/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include "IETFInterfaces.h"
#include "Rtnetlink.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;

namespace {

const auto CZECHLIGHT_NETWORK_MODULE_NAME = "czechlight-network"s;
const auto IETF_IP_MODULE_NAME = "ietf-ip"s;
const auto IETF_INTERFACES_MODULE_NAME = "ietf-interfaces"s;
const auto IETF_INTERFACES = "/"s + IETF_INTERFACES_MODULE_NAME + ":interfaces"s;

const auto PHYS_ADDR_LEN_STR_MAX = 17; // 48 bits is 6 bytes. One byte takes two chars, plus 5 delimiters.

std::string operStatusToString(uint8_t operStatus, velia::Log log)
{
    // unfortunately we can't use libnl rtnl_link_operstate2str, because it creates different strings than the YANG model expects
    switch (operStatus) {
    case IF_OPER_UP:
        return "up";
    case IF_OPER_DOWN:
        return "down";
    case IF_OPER_TESTING:
        return "testing";
    case IF_OPER_DORMANT:
        return "dormant";
    case IF_OPER_NOTPRESENT:
        return "not-present";
    case IF_OPER_LOWERLAYERDOWN:
        return "lower-layer-down";
    case IF_OPER_UNKNOWN:
    default:
        log->warn("Encountered unknown operational status {}, marking as 'unknown'", operStatus);
        return "unknown";
    }
}

std::string arpTypeToString(unsigned int arptype, velia::Log log)
{
    switch (arptype) {
    case ARPHRD_ETHER:
        return "iana-if-type:ethernetCsmacd";
    case ARPHRD_LOOPBACK:
        return "iana-if-type:softwareLoopback";
    case ARPHRD_SIT:
        return "iana-if-type:sixToFour";
    default:
        log->warn("Encountered unknown interface type {}, marking as 'iana-if-type:other'", arptype);
        return "iana-if-type:other";
    }
}

}

namespace velia::system {

IETFInterfaces::IETFInterfaces(std::shared_ptr<::sysrepo::Session> srSess, std::shared_ptr<Rtnetlink> rtnetlink)
    : m_rtnetlink(std::move(rtnetlink))
    , m_log(spdlog::get("system"))
    , m_srSubscribe(std::make_shared<sysrepo::Subscribe>(srSess))
{
    utils::ensureModuleImplemented(srSess, IETF_INTERFACES_MODULE_NAME, "2018-02-20");
    utils::ensureModuleImplemented(srSess, IETF_IP_MODULE_NAME, "2018-02-22");
    utils::ensureModuleImplemented(srSess, CZECHLIGHT_NETWORK_MODULE_NAME, "2021-02-22");

    m_srSubscribe->oper_get_items_subscribe(
        IETF_INTERFACES_MODULE_NAME.c_str(),
        [this](auto session, auto, auto, auto, auto, auto& parent) {
            std::map<std::string, std::string> data;

            m_rtnetlink->iterLinks([this, &data](rtnl_link* link) {
                char* name = rtnl_link_get_name(link);
                m_log->trace("Found link {}", name);

                std::array<char, PHYS_ADDR_LEN_STR_MAX + 1> buf;
                auto physAddrInternal = rtnl_link_get_addr(link);

                data["/ietf-interfaces:interfaces/interface[name='"s + name + "']/type"] = arpTypeToString(rtnl_link_get_arptype(link), m_log);
                data["/ietf-interfaces:interfaces/interface[name='"s + name + "']/phys-address"] = nl_addr2str(physAddrInternal, buf.data(), buf.size());
                data["/ietf-interfaces:interfaces/interface[name='"s + name + "']/oper-status"] = operStatusToString(rtnl_link_get_operstate(link), m_log);
            });

            utils::valuesToYang(data, session, parent);
            return SR_ERR_OK;
        },
        IETF_INTERFACES.c_str(),
        SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
}

}
