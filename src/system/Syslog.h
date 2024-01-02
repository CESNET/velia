/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include <optional>
#include <sdbus-c++/sdbus-c++.h>
#include <sysrepo-cpp/Connection.hpp>
#include "LLDP.h"
#include "utils/log-fwd.h"

namespace velia::system {

class Syslog {
public:
    Syslog(sysrepo::Connection conn, sdbus::IConnection& dbusConnection, const std::string& dbusBusName, const std::filesystem::path& journalUploadEnvFile);
    Syslog(sysrepo::Connection conn, sdbus::IConnection& dbusConnection, const std::filesystem::path& journalUploadEnvFile);

private:
    std::unique_ptr<sdbus::IProxy> m_sdManager;
    std::optional<sysrepo::Subscription> m_srSub;
    velia::Log m_log;
};

}
