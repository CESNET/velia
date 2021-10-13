/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
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
std::map<char, std::string> SYSTEM_CAPABILITIES = {
    {'o', "other"},
    {'p', "repeater"},
    {'b', "bridge"},
    {'w', "wlan-access-point"},
    {'r', "router"},
    {'t', "telephone"},
    {'d', "docsis-cable-device"},
    {'a', "station-only"},
    {'c', "cvlan-component"},
    {'s', "svlan-component"},
    {'m', "two-port-mac-relay"},
};

/** @brief Converts systemd's capabilities bitset to YANG's (named) bits.
 *
 * Apparently, libyang's parser requires the bits to be specified as string of names separated by whitespace.
 * See libyang's src/parser.c (function lyp_parse_value, switch-case LY_TYPE_BITS) and tests/test_sec9_7.c
 *
 * The names of individual bits should appear in the order they are defined in the YANG schema. At least that is how
 * I understand libyang's comment 'identifiers appear ordered by their position' in src/parser.c.
 * Systemd and our YANG model czechlight-lldp define the bits in the same order so this function does not have to care
 * about it.
 */
std::string toBitsYANG(const std::string& caps)
{
    std::string res;

    for (const auto& [bit, capability] : SYSTEM_CAPABILITIES) {
        if (std::find(caps.begin(), caps.end(), bit) != caps.end()) {
            if (!res.empty()) {
                res += " ";
            }

            res += capability;
        }
    }

    return res;
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

    for (const auto& [linkName, neighbors] : json.items()) {
        for (const auto& n_ : neighbors) {
            [[maybe_unused]] const auto& parameters = n_["neighbor"];
            NeighborEntry ne;
            ne.m_portId = linkName;

            if (auto it = parameters.find("chassisId"); it != parameters.end()) {
                ne.m_properties["remoteChassisId"] = *it;
            }
            if (auto it = parameters.find("portId"); it != parameters.end()) {
                ne.m_properties["remotePortId"] = *it;
            }
            if (auto it = parameters.find("systemName"); it != parameters.end()) {
                ne.m_properties["remoteSysName"] = *it;
            }
            if (auto it = parameters.find("enabledCapabilities"); it != parameters.end()) {
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
