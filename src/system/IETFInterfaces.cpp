/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <arpa/inet.h>
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
const auto IPV6ADDRSTRLEN_WITH_PREFIX = INET6_ADDRSTRLEN + 1 + 3 /* plus slash and max three-digits prefix */;

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

std::string nlActionToString(int action)
{
    switch (action) {
    case NL_ACT_NEW:
        return "NEW";
    case NL_ACT_DEL:
        return "DEL";
    case NL_ACT_CHANGE:
        return "CHANGE";
    case NL_ACT_UNSPEC:
        return "UNSPEC";
    case NL_ACT_GET:
        return "GET";
    case NL_ACT_SET:
        return "SET";
    default:
        return "<unknown action>";
    }
}

std::string binaddrToString(void* binaddr, int addrFamily)
{
    // any IPv4 address fits into a buffer allocated for an IPv6 address
    static_assert(INET6_ADDRSTRLEN >= INET_ADDRSTRLEN);
    std::array<char, INET6_ADDRSTRLEN> buf;

    if (const char* res = inet_ntop(addrFamily, binaddr, buf.data(), buf.size()); res != nullptr) {
        return res;
    } else {
        throw std::system_error {errno, std::generic_category(), "inet_ntop"};
    }
}

std::string getIPVersion(int addrFamily)
{
    switch (addrFamily) {
    case AF_INET:
        return "ipv4";
    case AF_INET6:
        return "ipv6";
    default:
        throw std::runtime_error("Unexpected address family " + std::to_string(addrFamily));
    }
}

}

namespace velia::system {

IETFInterfaces::IETFInterfaces(std::shared_ptr<::sysrepo::Session> srSess)
    : m_srSession(std::move(srSess))
    , m_log(spdlog::get("system"))
    , m_rtnetlink(std::make_shared<Rtnetlink>(
          [this](rtnl_link* link, int action) { onLinkUpdate(link, action); },
          [this](rtnl_addr* addr, int action) { onAddrUpdate(addr, action); },
          [this](rtnl_route* addr, int action) { onRouteUpdate(addr, action); }))
{
    utils::ensureModuleImplemented(m_srSession, IETF_INTERFACES_MODULE_NAME, "2018-02-20");
    utils::ensureModuleImplemented(m_srSession, IETF_IP_MODULE_NAME, "2018-02-22");
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_NETWORK_MODULE_NAME, "2021-02-22");

    m_rtnetlink->invokeInitialCallbacks();
    // TODO: Implement /ietf-routing:routing/interfaces and /ietf-routing:routing/router-id
}

void IETFInterfaces::onLinkUpdate(rtnl_link* link, int action)
{
    char* name = rtnl_link_get_name(link);
    m_log->trace("Netlink update on link '{}', action {}", name, nlActionToString(action));

    if (action == NL_ACT_DEL) {
        utils::valuesPush({}, {IETF_INTERFACES + "/interface[name='" + name + "']/"}, m_srSession, SR_DS_OPERATIONAL);
    } else if (action == NL_ACT_CHANGE || action == NL_ACT_NEW) {
        std::map<std::string, std::string> values;
        std::vector<std::string> deletePaths;

        auto linkAddr = rtnl_link_get_addr(link);
        std::array<char, PHYS_ADDR_BUF_SIZE> buf;
        if (auto physAddr = nl_addr2str(linkAddr, buf.data(), buf.size()); physAddr != "none"s && nl_addr_get_family(linkAddr) == AF_LLC) { // set physical address if the link has one
            values[IETF_INTERFACES + "/interface[name='" + name + "']/phys-address"] = physAddr;
        } else {
            // delete physical address from sysrepo if not provided by rtnetlink
            // Note: During testing I have noticed that my wireless interface loses a physical address. There were several change callbacks invoked
            // when simply bringing the interface down and up. In some of those, nl_addr2str returned "none".
            deletePaths.push_back({IETF_INTERFACES + "/interface[name='" + name + "']/phys-address"});
        }

        values[IETF_INTERFACES + "/interface[name='" + name + "']/type"] = arpTypeToString(rtnl_link_get_arptype(link), m_log);
        values[IETF_INTERFACES + "/interface[name='" + name + "']/oper-status"] = operStatusToString(rtnl_link_get_operstate(link), m_log);

        utils::valuesPush(values, deletePaths, m_srSession, SR_DS_OPERATIONAL);
    } else {
        m_log->warn("Unhandled cache update action {} ({})", action, nlActionToString(action));
    }
}

void IETFInterfaces::onAddrUpdate(rtnl_addr* addr, int action)
{
    std::unique_ptr<rtnl_link, std::function<void(rtnl_link*)>> link(rtnl_addr_get_link(addr), [](rtnl_link* obj) { nl_object_put(OBJ_CAST(obj)); });

    auto linkName = rtnl_link_get_name(link.get());
    auto addrFamily = rtnl_addr_get_family(addr);
    if (addrFamily != AF_INET && addrFamily != AF_INET6) {
        return;
    }

    m_log->trace("Netlink update on address of link '{}', action {}", linkName, nlActionToString(action));

    auto nlAddr = rtnl_addr_get_local(addr);
    std::string ipAddress = binaddrToString(nl_addr_get_binary_addr(nlAddr), addrFamily); // We don't use libnl's nl_addr2str because it appends a prefix length to the string (e.g. 192.168.0.1/24)
    std::string ipVersion = getIPVersion(addrFamily);

    std::map<std::string, std::string> values;
    std::vector<std::string> deletePaths;
    const auto yangPrefix = IETF_INTERFACES + "/interface[name='" + linkName + "']/ietf-ip:" + ipVersion + "/address[ip='" + ipAddress + "']";

    if (action == NL_ACT_DEL) {
        deletePaths.push_back({yangPrefix});
    } else if (action == NL_ACT_CHANGE || action == NL_ACT_NEW) {
        values[yangPrefix + "/prefix-length"] = std::to_string(rtnl_addr_get_prefixlen(addr));
    } else {
        m_log->warn("Unhandled cache update action {} ({})", action, nlActionToString(action));
    }

    utils::valuesPush(values, deletePaths, m_srSession, SR_DS_OPERATIONAL);
}

void IETFInterfaces::onRouteUpdate(rtnl_route*, int)
{
    /* NOTE:
     * We don't know the position of the changed route in the list of routes
     * Replace the whole subtree (and therefore fetch all routes to publish fresh data)
     * Unfortunately, this function may be called several times during the "reconstruction" of the routing table.
     */

    std::map<std::string, std::string> values;
    std::vector<std::string> deletePaths;

    auto routes = m_rtnetlink->getRoutes();
    auto links = m_rtnetlink->getLinks();

    std::map<decltype(AF_INET), unsigned> routeIdx {{AF_INET, 1}, {AF_INET6, 1}};

    for (const auto& route : routes) {
        if (rtnl_route_get_table(route.get()) != RT_TABLE_MAIN) {
            continue;
        }

        if (rtnl_route_get_type(route.get()) != RTN_UNICAST) {
            continue;
        }

        auto family = rtnl_route_get_family(route.get());
        if (family != AF_INET && family != AF_INET6) {
            continue;
        }

        auto proto = rtnl_route_get_protocol(route.get());
        if (proto != RTPROT_KERNEL && proto != RTPROT_RA && proto != RTPROT_DHCP && proto != RTPROT_STATIC) {
            std::array<char, 10> buf; /* "redirect" is max, see libnl/lib/route/route_utils.c: void init_proto_names() */
            m_log->warn("Unimplemented routing protocol {} '{}'", proto, rtnl_route_proto2str(proto, buf.data(), buf.size()));
            continue;
        }

        const auto ribName = family == AF_INET ? "ipv4-master"s : "ipv6-master"s;
        const auto yangPrefix = "/ietf-routing:routing/ribs/rib[name='" + ribName + "']/routes/route["s + std::to_string(routeIdx[family]++) + "]/";
        const auto familyYangPrefix = family == AF_INET ? "ietf-ipv4-unicast-routing"s : "ietf-ipv6-unicast-routing"s;

        std::string destPrefix;
        if (auto* addr = rtnl_route_get_dst(route.get()); addr != nullptr) {
            if (nl_addr_iszero(addr)) {
                destPrefix = family == AF_INET ? "0.0.0.0/0" : "::/0";
            } else {
                std::array<char, IPV6ADDRSTRLEN_WITH_PREFIX> data;
                destPrefix = nl_addr2str(addr, data.data(), data.size());

                // append prefix len if nl_addr2str fails to do that (when prefix length is 32 in ipv4 or 128 in ipv6)
                if (destPrefix.find_first_of('/') == std::string::npos) {
                    destPrefix += "/" + std::to_string(nl_addr_get_prefixlen(addr));
                }
            }
        }

        values[yangPrefix + familyYangPrefix + ":destination-prefix"] = destPrefix;

        auto scope = rtnl_route_get_scope(route.get());
        std::string protoStr;
        switch(proto) {
        case RTPROT_KERNEL:
            protoStr = scope == RT_SCOPE_LINK ? "direct" : "static";
            break;
        case RTPROT_STATIC:
            protoStr = "static";
            break;
        case RTPROT_DHCP:
            protoStr = "czechlight-network:dhcp";
            break;
        case RTPROT_RA:
            protoStr = "czechlight-network:ra";
            break;
        default:
            __builtin_unreachable();
        }

        values[yangPrefix + "source-protocol"] = protoStr;

        auto nexthopsCount = rtnl_route_get_nnexthops(route.get());
        assert(nexthopsCount == 1);
        rtnl_nexthop *nh = rtnl_route_nexthop_n(route.get(), 0);

        if (nl_addr *addr = rtnl_route_nh_get_gateway(nh); addr) {
            std::array<char, IPV6ADDRSTRLEN_WITH_PREFIX> buf;
            values[yangPrefix + "next-hop/" + familyYangPrefix + ":next-hop-address"] = nl_addr2str(addr, buf.data(), buf.size());
        }

        auto if_index = rtnl_route_nh_get_ifindex(nh);
        if (auto linkIt = std::find_if(links.begin(), links.end(), [if_index](const Rtnetlink::nlLink& link) { return rtnl_link_get_ifindex(link.get()) == if_index; }); linkIt != links.end()) {
            if (char* ifname = rtnl_link_get_name(linkIt->get()); ifname) {
                values[yangPrefix + "next-hop/outgoing-interface"] = rtnl_link_get_name(linkIt->get());
            }
        }
    }

    utils::valuesPush(values, deletePaths, m_srSession, SR_DS_OPERATIONAL);
}
}
