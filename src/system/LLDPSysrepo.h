/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <memory>
#include <optional>
#include <sysrepo-cpp/Session.hpp>
#include "LLDP.h"
#include "utils/log-fwd.h"

namespace velia::system {

class LLDPSysrepo {
public:
    explicit LLDPSysrepo(std::shared_ptr<LLDPDataProvider> lldp);
    sysrepo::ErrorCode operator()(sysrepo::Session session, uint32_t subscriptionId, const std::string& moduleName, const std::optional<std::string>& subXPath, const std::optional<std::string>& requestXPath, uint32_t, std::optional<libyang::DataNode>& output);

private:
    velia::Log m_log;
    std::shared_ptr<LLDPDataProvider> m_lldp;
};

}
