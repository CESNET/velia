/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include <functional>
#include <mutex>
#include <netlink/netlink.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/route/neighbour.h>
#include <stdexcept>
#include <thread>
#include "utils/log-fwd.h"

class rtnl_link;
class rtnl_neigh;

namespace velia::system {

namespace impl {
class nlCacheMngrWatcher;
}

/** @brief Wrapper for monitoring changes in NETLINK_ROUTE */
class Rtnetlink {
public:
    using nlCacheManager = std::shared_ptr<nl_cache_mngr>;
    using nlCache = std::unique_ptr<nl_cache, std::function<void(nl_cache*)>>;
    using nlLink = std::unique_ptr<rtnl_link, std::function<void(rtnl_link*)>>;
    using nlNeigh = std::unique_ptr<rtnl_neigh, std::function<void(rtnl_neigh*)>>;

    using LinkCB = std::function<void(rtnl_link* link, int cacheAction)>; /// cacheAction: NL_ACT_*
    using AddrCB = std::function<void(rtnl_addr* addr, int cacheAction)>; /// cacheAction: NL_ACT_*

    Rtnetlink(LinkCB cbLink, AddrCB cbAddr);
    ~Rtnetlink();
    std::vector<nlLink> getLinks();
    std::vector<std::pair<nlNeigh, nlLink>> getNeighbours();

private:
    velia::Log m_log;
    std::unique_ptr<nl_sock, std::function<void(nl_sock*)>> m_nlSocket;
    nlCacheManager m_nlCacheManager;
    LinkCB m_cbLink;
    AddrCB m_cbAddr;
    std::unique_ptr<impl::nlCacheMngrWatcher> m_nlCacheMngrWatcher; // first to destroy, because the thread uses m_nlCacheManager and m_log
};

class RtnetlinkException : public std::runtime_error {
public:
    RtnetlinkException(const std::string& msg);
    RtnetlinkException(const std::string& funcName, int error);
};

}
