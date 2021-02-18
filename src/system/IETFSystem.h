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
    IETFSystem(std::shared_ptr<::sysrepo::Session> srSession, const std::filesystem::path& osRelease);

private:
    void initStaticProperties(const std::filesystem::path& osRelease);
    void initSystemRestart();

    std::shared_ptr<::sysrepo::Session> m_srSession;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
    velia::Log m_log;
};
}
