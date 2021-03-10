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

namespace velia::system {

class Rtnetlink;

class IETFInterfaces {
public:
    IETFInterfaces(std::shared_ptr<::sysrepo::Session> srSessPush, std::shared_ptr<::sysrepo::Session> srSessPull);

private:
    void onLinkUpdate(rtnl_link* link, int action);
    void onAddrUpdate(rtnl_addr* addr, int action);

    std::shared_ptr<::sysrepo::Session> m_srSessionPush;
    std::shared_ptr<::sysrepo::Session> m_srSessionPull;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
    velia::Log m_log;
    std::shared_ptr<Rtnetlink> m_rtnetlink; // first to destroy, because the callback to rtnetlink uses m_srSession and m_log
};
}
