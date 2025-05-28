/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <sysrepo-cpp/Subscription.hpp>
#include <thread>
#include "utils/log-fwd.h"

namespace velia::system {

class LED {
public:
    LED(::sysrepo::Connection srConn, std::filesystem::path sysfsLeds);
    ~LED();

private:
    void poll() const;

    velia::Log m_log;
    std::map<std::filesystem::path, uint32_t> m_ledsMaxBrightness;
    ::sysrepo::Session m_srSession;
    std::optional<::sysrepo::Subscription> m_srSubscribe;
    std::thread m_thr;
    std::atomic<bool> m_thrRunning;
};
}
