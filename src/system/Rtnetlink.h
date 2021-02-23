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
#include <netlink/route/link.h>
#include <thread>
#include <stdexcept>
#include "utils/log-fwd.h"

namespace velia::system {

namespace impl {
class nlCacheMngrWatcher;
}

/** @brief Wrapper for monitoring changes in NETLINK_ROUTE */
class Rtnetlink {
public:
    using nlCacheManager = std::shared_ptr<nl_cache_mngr>;
    using LinkCB = std::function<void(rtnl_link* link, int cacheAction)>; /// cacheAction: NL_ACT_*

    explicit Rtnetlink(LinkCB cbLink);
    ~Rtnetlink();

private:
    velia::Log m_log;
    nlCacheManager m_nlCacheManager;
    LinkCB m_cbLink; /* (link object ptr, cache action). Cache action specified in libnl's cache.h (NL_ACT_*) */
    std::unique_ptr<impl::nlCacheMngrWatcher> m_nlCacheMngrWatcher; // first to destroy, because the thread uses m_nlCacheManager and m_log
};

class RtnetlinkException : public std::runtime_error {
public:
    RtnetlinkException(const std::string& msg);
    RtnetlinkException(const std::string& funcName, int error);
};

}
