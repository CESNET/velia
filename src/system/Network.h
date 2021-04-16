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
    using reload_cb_t = std::function<void(const std::vector<std::string>&)>;
    Network(std::shared_ptr<::sysrepo::Session> srSess, std::filesystem::path configDirectory, reload_cb_t reloadCallback);

private:
    std::filesystem::path configDirectory;
    reload_cb_t reloadCallback;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;

    int generateConfig(std::shared_ptr<::sysrepo::Session> session);
};
}
