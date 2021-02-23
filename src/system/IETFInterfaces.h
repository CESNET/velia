/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

struct rtnl_link;

namespace velia::system {

class Rtnetlink;

class IETFInterfaces {
public:
    IETFInterfaces(std::shared_ptr<::sysrepo::Session> srSess);

private:
    void onLinkUpdate(rtnl_link* link, int action);

    std::shared_ptr<::sysrepo::Session> m_srSession;
    velia::Log m_log;
    std::shared_ptr<Rtnetlink> m_rtnetlink; // first to destroy, because the callback to rtnetlink uses m_srSession and m_log
};
}
