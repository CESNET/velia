/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <netinet/ether.h>
#include <spdlog/spdlog.h>
#include "LLDP.h"
#include "utils/log.h"

namespace velia::system {

namespace impl {

static const std::string systemdNetworkdDbusInterface = "org.freedesktop.network1.Manager";
static const sdbus::ObjectPath systemdNetworkdDbusManagerObjectPath = "/org/freedesktop/network1";

/** @brief LLDP capabilities identifiers ordered by their appearence in YANG schema 'czechlight-lldp' */
const char* SYSTEM_CAPABILITIES[] = {
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
std::string toBitsYANG(uint16_t bits)
{
    std::string res;

    unsigned idx = 0;
    while (bits) {
        if (bits % 2) {
            if (!res.empty()) {
                res += " ";
            }
            res += SYSTEM_CAPABILITIES[idx];
        }

        bits /= 2;
        idx += 1;
    }

    return res;
}

/** @brief sd_lldp_neighbor requires deletion by invoking sd_lldp_neighbor_unrefp */
struct sd_lldp_neighbor_deleter {
    void operator()(sd_lldp_neighbor* e) const
    {
        sd_lldp_neighbor_unrefp(&e);
    }
};
using sd_lldp_neighbor_managed = std::unique_ptr<sd_lldp_neighbor, sd_lldp_neighbor_deleter>;

/* @brief Reads a LLDP neighbour entry from systemd's binary LLDP files.
*
* Inspired systemd's networkctl code.
*/
sd_lldp_neighbor_managed nextNeighbor(std::ifstream& ifs)
{
    size_t size;

    // read neighbor size
    /* Systemd allows the LLDP frame to be at most 4 KiB long. The comment in networkctl.c states that
     * "each LLDP packet is at most MTU size, but let's allow up to 4KiB just in case".
     * This comment may be misleading a bit because Ethernet Jumbo Frames can be up to 9000 B long.
     * However, LLDP frames should still be at most 1500 B long.
     * (see https://www.cisco.com/c/en/us/td/docs/routers/ncs4000/software/configure/guide/configurationguide/configurationguide_chapter_0111011.pdf)
     */
    {
        uint64_t rawSz; // get neighbour size in bytes
        ifs.read(reinterpret_cast<char*>(&rawSz), sizeof(rawSz));
        size = le64toh(rawSz);

        if (size_t rd = ifs.gcount(); (rd == 0 && ifs.eof()) || rd != sizeof(rawSz) || size >= 4096) {
            return nullptr;
        }
    }

    std::vector<uint8_t> raw;
    raw.resize(size);

    ifs.read(reinterpret_cast<char*>(raw.data()), size);
    if (static_cast<size_t>(ifs.gcount()) != size) { // typecast is safe here (see std::streamsize)
        return nullptr;
    }

    // let systemd parse from raw
    sd_lldp_neighbor* tmp = nullptr;
    if (sd_lldp_neighbor_from_raw(&tmp, raw.data(), size) < 0) {
        return nullptr;
    }

    return sd_lldp_neighbor_managed(tmp);
}

/* @brief Lists links using networkd dbus interface and returns them as a list of pairs <link_id, link_name>. */
auto listLinks(sdbus::IProxy* networkdManagerProxy)
{
    std::vector<sdbus::Struct<int, std::string, sdbus::ObjectPath>> links;
    std::vector<std::pair<int, std::string>> res; // we only want to return pairs (linkId, linkName), we do not need dbus object path

    networkdManagerProxy->callMethod("ListLinks").onInterface(impl::systemdNetworkdDbusInterface).storeResultsTo(links);

    std::transform(links.begin(), links.end(), std::back_inserter(res), [](const auto& e) { return std::make_pair(std::get<0>(e), std::get<1>(e)); });
    return res;
}

} /* namespace impl */

LLDPDataProvider::LLDPDataProvider(std::filesystem::path dataDirectory, sdbus::IConnection& dbusConnection, const std::string& dbusNetworkdBus)
    : m_log(spdlog::get("system"))
    , m_dataDirectory(std::move(dataDirectory))
    , m_networkdDbusProxy(sdbus::createProxy(dbusConnection, dbusNetworkdBus, impl::systemdNetworkdDbusManagerObjectPath))
{
}

std::vector<NeighborEntry> LLDPDataProvider::getNeighbors() const
{
    std::vector<NeighborEntry> res;

    for (const auto& [linkId, linkName] : impl::listLinks(m_networkdDbusProxy.get())) {
        m_log->debug("LLDP: Collecting neighbours on '{}' (id {})", linkName, linkId);

        // open lldp datafile
        std::filesystem::path lldpFilename = m_dataDirectory / std::to_string(linkId);
        std::ifstream ifs(lldpFilename, std::ios::binary);

        if (!ifs.is_open()) {
            // TODO: As of now, we are querying systemd-networkd for *all* links, not just those that have LLDP enabled.
            // TODO: Create a patch for systemd that queries *only* links with LLDP enabled and change severity of this debug log to warning/error.
            m_log->debug("  failed to open ({})", lldpFilename);
            continue;
        }

        while (auto n = impl::nextNeighbor(ifs)) {
            NeighborEntry ne;
            ne.m_portId = linkName;

            if (const char* system_name = nullptr; sd_lldp_neighbor_get_system_name(n.get(), &system_name) >= 0) {
                ne.m_properties["remoteSysName"] = system_name;
            }
            if (const char* port_id = nullptr; sd_lldp_neighbor_get_port_id_as_string(n.get(), &port_id) >= 0) {
                ne.m_properties["remotePortId"] = port_id;
            }
            if (const char* chassis_id = nullptr; sd_lldp_neighbor_get_chassis_id_as_string(n.get(), &chassis_id) >= 0) {
                ne.m_properties["remoteChassisId"] = chassis_id;
            }
            if (ether_addr* addr = nullptr; sd_lldp_neighbor_get_destination_address(n.get(), addr) >= 0) {
                ne.m_properties["remoteMgmtAddress"] = ether_ntoa(addr);
            }

            if (uint16_t cap = 0; sd_lldp_neighbor_get_system_capabilities(n.get(), &cap) >= 0) {
                ne.m_properties["systemCapabilitiesSupported"] = impl::toBitsYANG(cap);
            }

            if (uint16_t cap = 0; sd_lldp_neighbor_get_enabled_capabilities(n.get(), &cap) >= 0) {
                ne.m_properties["systemCapabilitiesEnabled"] = impl::toBitsYANG(cap);
            }

            m_log->trace("  found neighbor {}", ne);
            res.push_back(ne);
        }
    }

    return res;
}

std::ostream& operator<<(std::ostream& os, const NeighborEntry& entry)
{
    os << "lldp::lldp::NeighborEntry(" << entry.m_portId << ": {";

    for (auto it = entry.m_properties.begin(); it != entry.m_properties.end(); ++it) {
        if (it != entry.m_properties.begin()) {
            os << ", ";
        }

        os << it->first << ": " << it->second;
    }

    return os << "}";
}

}
