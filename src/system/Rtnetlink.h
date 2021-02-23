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

/** @brief Wrapper for monitoring changes in NETLINK_ROUTE */
class Rtnetlink {
public:
    explicit Rtnetlink(std::function<void(rtnl_link*, int)> cbLink);

private:
    using nlCacheManager = std::shared_ptr<nl_cache_mngr>;

    /** @brief Implementation of a background thread that watches for changes in cache and notifies caller via m_cbLink. */
    struct nlCacheMngrWatcher {
        bool m_terminate;
        std::mutex m_mtx;
        std::thread m_thr;

        nlCacheMngrWatcher(nlCacheManager manager, velia::Log log);
        ~nlCacheMngrWatcher();
        bool shouldTerminate();
        void run(Rtnetlink::nlCacheManager manager, velia::Log log);
    };

    velia::Log m_log;
    nlCacheManager m_nlCacheManager;
    std::function<void(rtnl_link*, int)> m_cbLink; /* (link object ptr, cache action). Cache action specified in libnl's cache.h (NL_ACT_*) */
    std::unique_ptr<nlCacheMngrWatcher> m_nlCacheMngrWatcher; // first to destroy, because the thread uses m_nlCacheManager and m_log
};

class RtnetlinkException : public std::runtime_error {
public:
    RtnetlinkException(const std::string& msg);
    RtnetlinkException(const std::string& funcName, int error);
};

}
