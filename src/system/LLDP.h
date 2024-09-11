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

namespace velia::system {

struct NeighborEntry {
    std::string m_portId;
    std::map<std::string, std::string> m_properties;
    bool operator==(const NeighborEntry& other) const = default;
};
std::ostream& operator<<(std::ostream& os, const NeighborEntry& entry);

class LLDPDataProvider {
public:
    explicit LLDPDataProvider(std::function<std::string()> dataCallback);
    std::vector<NeighborEntry> getNeighbors() const;

private:
    velia::Log m_log;
    std::function<std::string()> m_dataCallback;
};

}

template <>
struct fmt::formatter<velia::system::NeighborEntry> : ostream_formatter { };
