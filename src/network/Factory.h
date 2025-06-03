#pragma once
#include <filesystem>
#include <sdbus-c++/IConnection.h>
#include <sysrepo-cpp/Connection.hpp>
#include "network/IETFInterfaces.h"
#include "network/IETFInterfacesConfig.h"
#include "network/LLDP.h"
#include "network/LLDPSysrepo.h"
#include "network/SystemdNetworkdDbusClient.h"

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
    sdbus::IConnection& dbusConnection,
    const std::string& network1BusName,
    IETFInterfacesConfig::reload_cb_t runningNetworkReloadCB,
    LLDPDataProvider::data_callback_t lldpCallback,
    LLDPDataProvider::LocalData lldpLocalData)
{
    SystemdNetworkdDbusClient networkdDbusClient(dbusConnection, network1BusName, "/org/freedesktop/network1");
    auto managedLinks = networkdDbusClient.getManagedLinks();

    std::filesystem::create_directories(runtimeNetworkDirectory);
    std::filesystem::create_directories(persistentNetworkDirectory);
    auto running = conn.sessionStart(sysrepo::Datastore::Running);
    return {
        // IETFInterfaces has a background thread which acceses the session at random times
        .opsData = velia::network::IETFInterfaces{conn.sessionStart(sysrepo::Datastore::Operational)},
        .startupConfig = IETFInterfacesConfig{conn.sessionStart(sysrepo::Datastore::Startup), persistentNetworkDirectory, managedLinks, [](const auto&) {}},
        .runtimeConfig = IETFInterfacesConfig{running, runtimeNetworkDirectory, managedLinks, std::move(runningNetworkReloadCB)},
        .lldp = LLDPSysrepo{running,
                            std::make_shared<velia::network::LLDPDataProvider>(std::move(lldpCallback), std::move(lldpLocalData))},
    };
}
}
