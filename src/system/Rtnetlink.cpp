/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <netlink/route/link.h>

#include "Rtnetlink.h"
#include "utils/log.h"

namespace {

const auto PHYS_ADDR_LEN_STR_MAX = 17; // 48 bits is 6 bytes. One byte takes two chars, plus 5 delimiters.

using nlCache = std::unique_ptr<nl_cache, std::function<void(nl_cache*)>>;

/** @brief Wraps around nl_cache_foreach and rtnl_link type so that we can use lambdas with captures as a callback */
void nlCacheForeachWrapper(nlCache& cache, std::function<void(rtnl_link*)> cb)
{
    nl_cache_foreach(
        cache.get(), [](nl_object* obj, void* data) {
            auto& cb = *static_cast<std::function<void(rtnl_link*)>*>(data);
            auto link = reinterpret_cast<rtnl_link*>(obj);
            cb(link);
        },
        &cb);
}

}

namespace velia::system {

RtnetlinkException::RtnetlinkException(const std::string& funcName)
    : std::runtime_error("Rtnetlink call " + funcName + " failed")
{
}

Rtnetlink::Rtnetlink()
    : m_nlSocket(nl_socket_alloc(), nl_socket_free)
    , m_log(spdlog::get("system"))
{
    if (!m_nlSocket) {
        throw RtnetlinkException("nl_socket_alloc");
    }

    if (nl_connect(m_nlSocket.get(), NETLINK_ROUTE) < 0) {
        throw RtnetlinkException("nl_connect");
    }
}

/** @brief Returns network links list */
std::vector<Rtnetlink::LinkInfo> Rtnetlink::links() const
{
    nlCache linkCache;

    if (nl_cache* tmp; rtnl_link_alloc_cache(m_nlSocket.get(), AF_PACKET, &tmp) >= 0) {
        linkCache = nlCache(tmp, nl_cache_put);
    } else {
        throw RtnetlinkException("rtnl_link_alloc_cache");
    }

    std::vector<LinkInfo> res;

    nlCacheForeachWrapper(linkCache, [this, &res](rtnl_link* link) {
        char* name = rtnl_link_get_name(link);

        std::string physAddress;
        {
            std::array<char, PHYS_ADDR_LEN_STR_MAX + 1> buf;
            auto physAddrInternal = rtnl_link_get_addr(link);
            physAddress = nl_addr2str(physAddrInternal, buf.data(), buf.size());
        }

        auto operStatus = rtnl_link_get_operstate(link);

        m_log->trace("Found link {}: phys-address={}", name, physAddress);
        res.push_back({name, std::move(physAddress), operStatus});
    });

    return res;
}

}
