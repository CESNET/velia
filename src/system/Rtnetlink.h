/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <stdexcept>
#include "utils/log-fwd.h"

namespace velia::system {

/**
 * @brief Readout of values via netlink protocol.
 */
class Rtnetlink {
public:
    explicit Rtnetlink(std::function<void(rtnl_link*, int)> cbLink);
    ~Rtnetlink();

private:
    using nlCacheManager = std::unique_ptr<nl_cache_mngr, std::function<void(nl_cache_mngr*)>>;
    struct nlCacheManagerChangesThread {
        std::thread m_thr;
        bool m_terminate = false;
        std::mutex m_mtx;

        bool shouldTerminate();
        void join();
    };

    nlCacheManager m_nlCacheManager;
    velia::Log m_log;
    std::function<void(rtnl_link*, int)> m_cbLink; /* (link object ptr, cache action). Cache action specified in libnl's cache.h (NL_ACT_*) */
    nlCacheManagerChangesThread m_changes;
};

class RtnetlinkException : public std::runtime_error {
public:
    RtnetlinkException(const std::string& funcName);
    RtnetlinkException(const std::string& funcName, int error);
};

}
