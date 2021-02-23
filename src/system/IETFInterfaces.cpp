/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

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

std::string operStatusToString(uint8_t operStatus)
{
    // unfortunately we can't use libnl rtnl_link_operstate2str, because it creates different strings than the YANG model expects
    switch (operStatus) {
    case IF_OPER_UP:
        return "up";
    case IF_OPER_DOWN:
        return "down";
    case IF_OPER_TESTING:
        return "testing";
    case IF_OPER_UNKNOWN:
        return "unknown";
    case IF_OPER_DORMANT:
        return "dormant";
    case IF_OPER_NOTPRESENT:
        return "not-present";
    case IF_OPER_LOWERLAYERDOWN:
        return "lower-layer-down";
    }

    __builtin_unreachable();
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

            for (const auto& interface : m_rtnetlink->links()) {
                data["/ietf-interfaces:interfaces/interface[name='" + interface.m_name + "']/type"] = "iana-if-type:ethernetCsmacd"; // TODO: Are all interfaces CSMA/CD (optical ports, loopback)? Pass mapping from main?
                data["/ietf-interfaces:interfaces/interface[name='" + interface.m_name + "']/phys-address"] = interface.m_physAddr;
                data["/ietf-interfaces:interfaces/interface[name='" + interface.m_name + "']/oper-status"] = operStatusToString(interface.m_operStatus);
            }

            utils::valuesToYang(data, session, parent);
            return SR_ERR_OK;
        },
        IETF_INTERFACES.c_str(),
        SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
}

}
