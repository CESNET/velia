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

class Sysrepo {
public:
    explicit Sysrepo(std::shared_ptr<::sysrepo::Session> srSession, const std::filesystem::path& osRelease);

private:
    std::shared_ptr<::sysrepo::Session> m_srSession;
    velia::Log m_log;
};
}
