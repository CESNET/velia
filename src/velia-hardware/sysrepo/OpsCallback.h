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
#include "HardwareState.h"

namespace velia::hardware::sysrepo {

class OpsCallback {
public:
    explicit OpsCallback(std::shared_ptr<HardwareState> driver);
    int operator()(std::shared_ptr<::sysrepo::Session> session, const char* module_name, const char* path, const char* request_xpath, uint32_t request_id, std::shared_ptr<libyang::Data_Node>& parent);

private:
    std::shared_ptr<HardwareState> m_hwState;
    uint64_t m_lastRequestId;
};
}
