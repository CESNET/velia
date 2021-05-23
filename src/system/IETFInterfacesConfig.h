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

class Rtnetlink;

class IETFInterfacesConfig {
public:
    using reload_cb_t = std::function<void(const std::vector<std::string>&)>;
    explicit IETFInterfacesConfig(std::shared_ptr<::sysrepo::Session> srSess, std::filesystem::path configDirectory, std::vector<std::string> managedLinks, reload_cb_t reloadCallback);

private:
    velia::Log m_log;
    reload_cb_t m_reloadCb;
    std::filesystem::path m_configDirectory;
    std::vector<std::string> m_managedLinks;
    std::shared_ptr<::sysrepo::Session> m_srSession;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;

    int onUpdate(std::shared_ptr<::sysrepo::Session> session) const;
    std::vector<std::string> writeConfigs(const std::map<std::string, std::string>& networkConfig, const std::filesystem::path& configDir) const;
};
}
