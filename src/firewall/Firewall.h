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
    SysrepoFirewall(sysrepo::S_Session srSess, NftConfigConsumer consumer);

private:
    sysrepo::S_Session m_session;
    sysrepo::S_Subscribe m_sub;
    velia::Log m_log;
};
}
