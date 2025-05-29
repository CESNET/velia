/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <memory>
#include <optional>
#include <sysrepo-cpp/Subscription.hpp>
#include "LLDP.h"
#include "utils/log-fwd.h"

namespace velia::network {

class LLDPSysrepo {
public:
    explicit LLDPSysrepo(sysrepo::Session& session, std::shared_ptr<LLDPDataProvider> lldp);
    void fetch(sysrepo::Session session, std::optional<libyang::DataNode>& output);

private:
    velia::Log m_log;
    std::shared_ptr<LLDPDataProvider> m_lldp;
    sysrepo::Subscription m_sub;
};

}
