/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <filesystem>
#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::system {

class Network {
public:
    Network(std::shared_ptr<::sysrepo::Connection> srConn, std::filesystem::path runtimeNetworkDirectory, std::function<void(const std::vector<std::string>&)> networkReloadCallback);

private:
    velia::Log m_log;
    std::shared_ptr<::sysrepo::Connection> m_srConn;
    std::shared_ptr<::sysrepo::Session> m_srSessionRunning;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribeRunning;
};
}
