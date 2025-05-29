/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <filesystem>
#include <map>
#include <sysrepo-cpp/Subscription.hpp>
#include "utils/log-fwd.h"

namespace velia::network {

class Rtnetlink;

class IETFInterfacesConfig {
public:
    struct ChangedUnits {
        using Vector = std::vector<std::string>;
        Vector deleted;
        Vector changedOrNew;
        bool operator==(const ChangedUnits& other) const noexcept = default;
    };
    using reload_cb_t = std::function<void(const ChangedUnits&)>;
    explicit IETFInterfacesConfig(::sysrepo::Session srSess, std::filesystem::path configDirectory, std::vector<std::string> managedLinks, reload_cb_t reloadCallback);

private:
    velia::Log m_log;
    reload_cb_t m_reloadCb;
    std::filesystem::path m_configDirectory;
    std::vector<std::string> m_managedLinks;
    ::sysrepo::Session m_srSession;
    std::optional<::sysrepo::Subscription> m_srSubscribe;

    sysrepo::ErrorCode moduleChange(::sysrepo::Session session) const;
    ChangedUnits updateNetworkFiles(const std::map<std::string, std::string>& networkConfig, const std::filesystem::path& configDir) const;
};
}
