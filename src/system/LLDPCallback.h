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
    int operator()(std::shared_ptr<::sysrepo::Session> session, const char* module_name, const char* path, const char* request_xpath, uint32_t request_id, std::shared_ptr<libyang::Data_Node>& parent);

private:
    velia::Log m_log;
    std::shared_ptr<LLDPDataProvider> m_lldp;
    uint64_t m_lastRequestId;
};

}
