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
    Network(std::shared_ptr<::sysrepo::Session> srSess, std::filesystem::path networkConfigDirectory, std::function<void(const std::vector<std::string>&)> networkReloadCallback);

private:
    velia::Log m_log;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
};
}
