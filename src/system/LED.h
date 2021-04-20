/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <filesystem>
#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::system {

class LED {
public:
    LED(const std::shared_ptr<::sysrepo::Connection>& srConn, std::filesystem::path  sysfsLeds);

private:
    velia::Log m_log;
    std::filesystem::path m_sysfsLeds;
    std::shared_ptr<::sysrepo::Session> m_srSession;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
};
}
