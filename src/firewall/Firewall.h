/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::firewall {
class SysrepoFirewall {
public:
    using NftConfigConsumer = std::function<void(const std::string& config)>;
    SysrepoFirewall(sysrepo::Session srSess, NftConfigConsumer consumer);

private:
    // FIXME: this optional does kinda suck. But I need it so that I don't have IIFE in the ctor initializer list.
    std::optional<sysrepo::Subscription> m_sub;
    velia::Log m_log;
};
}
