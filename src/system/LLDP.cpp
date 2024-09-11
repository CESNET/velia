/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <boost/algorithm/string/join.hpp>
#include <netinet/ether.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "LLDP.h"
#include "system_vars.h"
#include "utils/exec.h"
#include "utils/log.h"

namespace velia::system {

namespace {

/** @brief LLDP capabilities identifiers ordered by their appearence in YANG schema 'czechlight-lldp' */
std::vector<std::string> SYSTEM_CAPABILITIES = {
    "other",
    "repeater",
    "bridge",
    "wlan-access-point",
    "router",
    "telephone",
    "docsis-cable-device",
    "station-only",
    "cvlan-component",
    "svlan-component",
    "two-port-mac-relay",
};

/** @brief Converts capabilities bits to YANG's (named) bits.
 *
 * Apparently, libyang's parser requires the bits to be specified as string of names separated by whitespace.
 * See libyang's src/parser.c (function lyp_parse_value, switch-case LY_TYPE_BITS) and tests/test_sec9_7.c
 *
 * The names of individual bits should appear in the order they are defined in the YANG schema. At least that is how
 * I understand libyang's comment 'identifiers appear ordered by their position' in src/parser.c.
 * LLDP and our YANG model czechlight-lldp define the bits in the same order so this function does not have to care
 * about it.
 */
std::string toBitsYANG(const int caps)
{
    std::vector<std::string> res;

    for (size_t i = 0; i < SYSTEM_CAPABILITIES.size(); i++) {
        if (caps & (1U << i)) {
            res.push_back(SYSTEM_CAPABILITIES[i]);
        }
    }

    return boost::algorithm::join(res, " ");
}
}

LLDPDataProvider::LLDPDataProvider(std::function<std::string()> dataCallback)
    : m_log(spdlog::get("system"))
    , m_dataCallback(std::move(dataCallback))
{
}

std::vector<NeighborEntry> LLDPDataProvider::getNeighbors() const
{
    std::vector<NeighborEntry> res;

    auto json = nlohmann::json::parse(m_dataCallback());

    for (const auto& interface: json["Neighbors"]) {
        auto linkName = interface["InterfaceName"].get<std::string>();

        for (const auto& neighbor : interface["Neighbors"]) {
            NeighborEntry ne;
            ne.m_portId = linkName;

            if (auto it = neighbor.find("ChassisID"); it != neighbor.end()) {
                ne.m_properties["remoteChassisId"] = *it;
            }
            if (auto it = neighbor.find("PortID"); it != neighbor.end()) {
                ne.m_properties["remotePortId"] = *it;
            }
            if (auto it = neighbor.find("SystemName"); it != neighbor.end()) {
                ne.m_properties["remoteSysName"] = *it;
            }
            if (auto it = neighbor.find("EnabledCapabilities"); it != neighbor.end()) {
                ne.m_properties["systemCapabilitiesEnabled"] = toBitsYANG(*it);
            }

            m_log->trace("Found LLDP neighbor {}", ne);
            res.push_back(ne);
        }
    }

    return res;
}

std::ostream& operator<<(std::ostream& os, const NeighborEntry& entry)
{
    os << "NeighborEntry(" << entry.m_portId << ": {";

    for (auto it = entry.m_properties.begin(); it != entry.m_properties.end(); ++it) {
        if (it != entry.m_properties.begin()) {
            os << ", ";
        }

        os << it->first << ": " << it->second;
    }

    return os << "})";
}

}
