/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <filesystem>
#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::system {

class IETFSystem {
public:
    IETFSystem(std::shared_ptr<::sysrepo::Connection> srSession, const std::filesystem::path& osRelease);

private:
    std::shared_ptr<::sysrepo::Session> m_srSessionRunning;
    std::shared_ptr<::sysrepo::Session> m_srSessionStartup;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribeRunning;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribeStartup;
    velia::Log m_log;
};
}
