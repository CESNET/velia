/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <mutex>
#include <sysrepo-cpp/Subscription.hpp>
#include "utils/log-fwd.h"

struct rtnl_link;
struct rtnl_addr;
struct rtnl_route;

namespace velia::network {

class Rtnetlink;

class IETFInterfaces {
public:
    explicit IETFInterfaces(::sysrepo::Session srSess);

private:
    void onLinkUpdate(rtnl_link* link, int action);
    void onAddrUpdate(rtnl_addr* addr, int action);
    void onRouteUpdate(rtnl_route* addr, int action);

    ::sysrepo::Session m_srSession;
    std::optional<::sysrepo::Subscription> m_srSubscribe;
    velia::Log m_log;
    std::mutex m_mtx;
    std::shared_ptr<Rtnetlink> m_rtnetlink; // first to destroy, because the callback to rtnetlink uses m_srSession and m_log
};
}
