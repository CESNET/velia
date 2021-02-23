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

const auto PHYS_ADDR_BUF_SIZE = 6 * 2 /* 2 chars per 6 bytes in the address */ + 5 /* delimiters (':') between bytes */ + 1 /* \0 */;

std::string operStatusToString(uint8_t operStatus, velia::Log log)
{
    // unfortunately we can't use libnl's rtnl_link_operstate2str, because it creates different strings than the YANG model expects
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
        return "unknown";
    default:
        log->warn("Encountered unknown operational status {}, using 'unknown'", operStatus);
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
        log->warn("Encountered unknown interface type {}, using 'iana-if-type:other'", arptype);
        return "iana-if-type:other";
    }
}

}

namespace velia::system {

IETFInterfaces::IETFInterfaces(std::shared_ptr<::sysrepo::Session> srSess)
    : m_srSession(std::move(srSess))
    , m_log(spdlog::get("system"))
    , m_rtnetlink(std::make_shared<Rtnetlink>([this](rtnl_link* link, int action) {
        char* name = rtnl_link_get_name(link);
        m_log->trace("Cache update on link '{}', action {}", name, action);

        if (action == NL_ACT_DEL) {
            auto oldDs = m_srSession->session_get_ds();
            m_srSession->session_switch_ds(SR_DS_OPERATIONAL);
            m_log->trace("DEL {}", IETF_INTERFACES + "/interface[name='" + name + "']");
            m_srSession->delete_item((IETF_INTERFACES + "/interface[name='" + name + "']").c_str());
            m_srSession->apply_changes();
            m_srSession->session_switch_ds(oldDs);
        } else if (action == NL_ACT_CHANGE || action == NL_ACT_NEW) {
            std::map<std::string, std::string> data;

            std::array<char, PHYS_ADDR_BUF_SIZE> buf;
            auto physAddrInternal = rtnl_link_get_addr(link);
            auto physAddr = nl_addr2str(physAddrInternal, buf.data(), buf.size());
            if (physAddr != "none"s) {
                data[IETF_INTERFACES + "/interface[name='" + name + "']/phys-address"] = physAddr;
            } else {
                m_log->trace("DEL {}", IETF_INTERFACES + "/interface[name='" + name + "']/phys-address");
                m_srSession->delete_item((IETF_INTERFACES + "/interface[name='" + name + "']/phys-address").c_str());
                m_srSession->apply_changes();
            }

            data[IETF_INTERFACES + "/interface[name='" + name + "']/type"] = arpTypeToString(rtnl_link_get_arptype(link), m_log);
            data[IETF_INTERFACES + "/interface[name='" + name + "']/oper-status"] = operStatusToString(rtnl_link_get_operstate(link), m_log);

            for (const auto& [k, v] : data) {
                m_log->trace("PUSH {} -> {}", k, v);
            }

            utils::valuesPush(data, m_srSession, SR_DS_OPERATIONAL);
        } else {
            m_log->warn("Unhandled cache update action {}", action);
        }
    }))
{
    utils::ensureModuleImplemented(m_srSession, IETF_INTERFACES_MODULE_NAME, "2018-02-20");
    utils::ensureModuleImplemented(m_srSession, IETF_IP_MODULE_NAME, "2018-02-22");
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_NETWORK_MODULE_NAME, "2021-02-22");
}

}
