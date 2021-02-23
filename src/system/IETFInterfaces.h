/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::system {

class Rtnetlink;

class IETFInterfaces {
public:
    IETFInterfaces(std::shared_ptr<::sysrepo::Session> srSess, std::shared_ptr<Rtnetlink> rtnetlink);

private:
    std::shared_ptr<Rtnetlink> m_rtnetlink;
    velia::Log m_log;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
};
}
