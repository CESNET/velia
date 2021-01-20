/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <filesystem>
#include <sysrepo-cpp/Session.hpp>
#include "RAUC.h"
#include "utils/log-fwd.h"

namespace velia::system {

class Sysrepo {
public:
    explicit Sysrepo(std::shared_ptr<::sysrepo::Session> srSession, const std::filesystem::path& osRelease, std::shared_ptr<RAUC> rauc);

private:
    std::shared_ptr<::sysrepo::Session> m_srSession;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
    std::shared_ptr<RAUC> m_rauc;
    velia::Log m_log;
};
}
