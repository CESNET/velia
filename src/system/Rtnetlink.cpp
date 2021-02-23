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

const auto MAX_PHYS_ADDR_LEN = 17; // 48 bits is 6 bytes. One byte takes two chars, plus 5 delimiters.

std::vector<rtnl_link*> collectNlCacheObjects(nl_cache* cache)
{
    std::vector<rtnl_link*> ret;
    for (nl_object* obj = nl_cache_get_first(cache); obj != nullptr ; obj = nl_cache_get_next(obj)) {
        ret.push_back(reinterpret_cast<rtnl_link*>(obj));
    }
    return ret;
}

}

namespace velia::system {

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

std::vector<Rtnetlink::LinkInfo> Rtnetlink::links() const
{
    std::unique_ptr<nl_cache, std::function<void(nl_cache*)>> linkCache(nullptr, nl_cache_free);

    if (nl_cache* tmp; rtnl_link_alloc_cache(m_nlSocket.get(), AF_PACKET, &tmp) >= 0) {
        linkCache.reset(tmp);
    } else {
        throw RtnetlinkException("rtnl_link_alloc_cache");
    }

    auto links = collectNlCacheObjects(linkCache.get());
    m_log->trace("Found {} network links.", nl_cache_nitems(linkCache.get()));

    std::vector<LinkInfo> ret;
    for (const auto link : links) {
        char* name = rtnl_link_get_name(link);

        std::string addr;
        {
            std::array<char, MAX_PHYS_ADDR_LEN + 1> buf;
            auto addrInternal = rtnl_link_get_addr(link);
            addr = nl_addr2str(addrInternal, buf.data(), buf.size());
        }

        auto operStatus = rtnl_link_get_operstate(link);

        if (m_log->should_log(spdlog::level::trace)) {
            std::array<char, 100> buf;
            char* operStatusStr = rtnl_link_operstate2str(operStatus, buf.data(), buf.size());
            m_log->trace("Found link {}: phys-address={}, oper-state={}", name, addr, operStatusStr);
        }

        ret.push_back({name, std::move(addr), operStatus});
    }

    return ret;
}
}
