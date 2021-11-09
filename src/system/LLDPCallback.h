/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sysrepo-cpp/Session.hpp>
#include "LLDP.h"
#include "utils/log-fwd.h"

namespace velia::system {

class LLDPCallback {
public:
    explicit LLDPCallback(std::shared_ptr<LLDPDataProvider> lldp);
    sysrepo::ErrorCode operator()(sysrepo::Session session, uint32_t subscriptionId, std::string_view moduleName, std::optional<std::string_view> subXPath, std::optional<std::string_view> requestXPath, uint32_t, std::optional<libyang::DataNode>& output);

private:
    velia::Log m_log;
    std::shared_ptr<LLDPDataProvider> m_lldp;
};

}
