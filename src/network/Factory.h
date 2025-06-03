#pragma once
#include <filesystem>
#include <sysrepo-cpp/Connection.hpp>
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
    sysrepo::Connection conn,
    const std::filesystem::path& persistentNetworkDirectory,
    const std::filesystem::path& runtimeNetworkDirectory,
    const std::vector<std::string>& managedLinks,
    IETFInterfacesConfig::reload_cb_t runningNetworkReloadCB,
    LLDPDataProvider::data_callback_t lldpCallback,
    LLDPDataProvider::LocalData lldpLocalData);
}
