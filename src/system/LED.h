/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <sysrepo-cpp/Session.hpp>
#include <thread>
#include "utils/log-fwd.h"

namespace velia::system {

class LED {
public:
    LED(const std::shared_ptr<::sysrepo::Connection>& srConn, std::filesystem::path sysfsLeds);
    ~LED();

private:
    void poll() const;

    velia::Log m_log;
    std::map<std::filesystem::path, uint32_t> m_ledsMaxBrightness;
    std::shared_ptr<::sysrepo::Session> m_srSession;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
    std::thread m_thr;
    std::atomic<bool> m_thrRunning;
};
}
