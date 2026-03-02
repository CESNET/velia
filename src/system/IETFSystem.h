/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <filesystem>
#include <sdbus-c++/sdbus-c++.h>
#include <sysrepo-cpp/Subscription.hpp>
#include "utils/log-fwd.h"

namespace velia::system {

class IETFSystem {
public:
    struct SystemdConfigData {
        std::filesystem::path runtimeDir;
        std::function<void()> reload;
    };

    IETFSystem(::sysrepo::Session srSession,
               const std::filesystem::path& osRelease,
               const std::filesystem::path& procStat,
               sdbus::IConnection& dbusConnection,
               const std::string& dbusName,
               const SystemdConfigData& resolved);

private:
    void initStaticProperties(const std::filesystem::path& osRelease);
    void initSystemRestart();
    void initHostname();
    void initDummies();
    void initClock(const std::filesystem::path& procStatPath);
    void initDNS(sdbus::IConnection& connection, const std::string& dbusName, const SystemdConfigData& resolved);
    void initNTP(sdbus::IConnection& connection, const std::string& dbusName);

    ::sysrepo::Session m_srSession;
    std::optional<::sysrepo::Subscription> m_srSubscribe;
    velia::Log m_log;
};
}
