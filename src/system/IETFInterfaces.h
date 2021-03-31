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
struct rtnl_addr;
struct rtnl_route;

namespace velia::system {

class Rtnetlink;

class IETFInterfaces {
public:
    explicit IETFInterfaces(std::shared_ptr<::sysrepo::Session> srSess);

private:
    void onLinkUpdate(rtnl_link* link, int action);
    void onAddrUpdate(rtnl_addr* addr, int action);
    void onRouteUpdate(rtnl_route* addr, int action);

    std::shared_ptr<::sysrepo::Session> m_srSession;
    velia::Log m_log;
    std::shared_ptr<Rtnetlink> m_rtnetlink; // first to destroy, because the callback to rtnetlink uses m_srSession and m_log
};
}
