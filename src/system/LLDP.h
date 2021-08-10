/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/fmt/ostr.h> // allow spdlog to use operator<<(ostream, NeighborEntry)
#include <string>
#include <systemd/sd-lldp.h>
#include <vector>
#include "utils/log-fwd.h"

namespace velia::system {

struct NeighborEntry {
    std::string m_portId;
    std::map<std::string, std::string> m_properties;
};
std::ostream& operator<<(std::ostream& os, const NeighborEntry& entry);

class LLDPDataProvider {
public:
    LLDPDataProvider(std::filesystem::path dataDirectory, sdbus::IConnection& dbusConnection, const std::string& dbusNetworkdBus);
    std::vector<NeighborEntry> getNeighbors() const;

private:
    velia::Log m_log;
    std::filesystem::path m_dataDirectory;
    std::unique_ptr<sdbus::IProxy> m_networkdDbusProxy;
};

}
