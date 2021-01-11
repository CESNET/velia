/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::ietf_system::sysrepo {

class Sysrepo {
public:
    explicit Sysrepo(std::shared_ptr<::sysrepo::Session> srSession, std::shared_ptr<RAUC> rauc);

private:
    std::shared_ptr<::sysrepo::Session> m_srSession;
    velia::Log m_log;
};
}
