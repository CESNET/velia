#pragma once
#include <filesystem>
#include <sysrepo-cpp/Session.hpp>
#include "network/IETFInterfaces.h"
#include "network/IETFInterfacesConfig.h"
#include "network/LLDP.h"
#include "network/LLDPSysrepo.h"

namespace velia::network {
struct Services {
    IETFInterfaces opsData;
    IETFInterfacesConfig startupConfig, runtimeConfig;
    LLDPSysrepo lldp;
};

Services create(
    sysrepo::Session netlinkSess,
    sysrepo::Session startupSess,
    const std::filesystem::path& persistentNetworkDirectory,
    sysrepo::Session runningSess,
    const std::filesystem::path& runtimeNetworkDirectory,
    const std::vector<std::string>& managedLinks,
    IETFInterfacesConfig::reload_cb_t runningNetworkReloadCB,
    LLDPDataProvider::data_callback_t lldpCallback,
    LLDPDataProvider::LocalData lldpLocalData)
{
    std::filesystem::create_directories(runtimeNetworkDirectory);
    std::filesystem::create_directories(persistentNetworkDirectory);
    return {
        .opsData = velia::network::IETFInterfaces{netlinkSess},
        .startupConfig = IETFInterfacesConfig{startupSess, persistentNetworkDirectory, managedLinks, [](const auto&) {}},
        .runtimeConfig = IETFInterfacesConfig{runningSess, runtimeNetworkDirectory, managedLinks, std::move(runningNetworkReloadCB)},
        .lldp = LLDPSysrepo{runningSess,
                            std::make_shared<velia::network::LLDPDataProvider>(std::move(lldpCallback), std::move(lldpLocalData))},
    };
}
}
