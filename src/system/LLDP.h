/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <functional>
#include <map>
#include <spdlog/fmt/ostr.h> // allow spdlog to use operator<<(ostream, NeighborEntry)
#include <string>
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
    explicit LLDPDataProvider(std::function<std::string()> dataCallback);
    std::vector<NeighborEntry> getNeighbors() const;

private:
    velia::Log m_log;
    std::function<std::string()> m_dataCallback;
};

}
