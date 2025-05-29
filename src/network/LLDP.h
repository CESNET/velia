/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <fmt/ostream.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "utils/log-fwd.h"

namespace velia::network {

struct NeighborEntry {
    std::string m_portId;
    std::map<std::string, std::string> m_properties;
    bool operator==(const NeighborEntry& other) const = default;
};
std::ostream& operator<<(std::ostream& os, const NeighborEntry& entry);

class LLDPDataProvider {
public:
    /** @brief Static data containing information that are sent by the LLDP protocol about local machine. */
    struct LocalData {
        std::string chassisId;
        std::string chassisSubtype;
    };

    explicit LLDPDataProvider(std::function<std::string()> dataCallback, const LocalData& localData);
    std::vector<NeighborEntry> getNeighbors() const;
    std::map<std::string, std::string> localProperties() const;

private:
    velia::Log m_log;
    std::function<std::string()> m_dataCallback;
    LocalData m_localData;
};

}

template <>
struct fmt::formatter<velia::network::NeighborEntry> : ostream_formatter { };
