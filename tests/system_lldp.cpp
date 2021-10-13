#include "trompeloeil_doctest.h"
#include "pretty_printers.h"
#include "system/LLDP.h"
#include "system_vars.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "utils/exec.h"

using namespace std::literals;

namespace velia::system {
bool operator==(const NeighborEntry& a, const NeighborEntry& b)
{
    return std::tie(a.m_portId, a.m_properties) == std::tie(b.m_portId, b.m_properties);
}
}

TEST_CASE("Parsing with the mock")
{
    TEST_INIT_LOGS;

    std::vector<velia::system::NeighborEntry> expected;
    std::string json;

    SECTION("LLDP active on a single link")
    {
        json = R"({"ve-image": [{"neighbor": {"systemName": "image", "portId": "host0", "chassisId": "7062a9e41c924ac6942da39c56e6b820", "enabledCapabilities": "a"}}]})";
        expected = {
            {"ve-image", {
                             {"remoteSysName", "image"},
                             {"remotePortId", "host0"},
                             {"remoteChassisId", "7062a9e41c924ac6942da39c56e6b820"},
                             {"systemCapabilitiesEnabled", "station-only"},
                         }}};
    }

    SECTION("No LLDP enabled")
    {
        json = "{}";
        expected = {};
    }

    SECTION("Two LLDP links")
    {
        json = R"({
"enp0s31f6": [{"neighbor": {"systemName": "sw-a1128-01.fit.cvut.cz", "portId": "Gi3/0/7", "chassisId": "00:b8:b3:e6:17:80", "enabledCapabilities": "b"}}],
"ve-image":  [{"neighbor": {"systemName": "image", "portId": "host0", "chassisId": "8b90f96f448140fb9b5d9d68e86d052e", "enabledCapabilities": "a"}}]
})";
        expected = {
            {"enp0s31f6", {
                              {"remoteSysName", "sw-a1128-01.fit.cvut.cz"},
                              {"remotePortId", "Gi3/0/7"},
                              {"remoteChassisId", "00:b8:b3:e6:17:80"},
                              {"systemCapabilitiesEnabled", "bridge"},
                          }},
            {"ve-image", {
                             {"remoteSysName", "image"},
                             {"remotePortId", "host0"},
                             {"remoteChassisId", "8b90f96f448140fb9b5d9d68e86d052e"},
                             {"systemCapabilitiesEnabled", "station-only"},
                         }},
        };
    }

    SECTION("Multiple neighbors on one interface")
    {
        json = R"({"host0": [
{"neighbor": {"systemName": "image", "portId": "host0", "chassisId": "1631331c24bb499bb644fcdf7c9fd467", "enabledCapabilities": "a"}},
{"neighbor": {"systemName": "enterprise", "portId": "vb-image2", "chassisId": "1efe5cecbfc248a09065ad6177a98b41", "enabledCapabilities": "a"}}
]})";

        expected = {
            {"host0", {
                          {"remoteSysName", "image"},
                          {"remotePortId", "host0"},
                          {"remoteChassisId", "1631331c24bb499bb644fcdf7c9fd467"},
                          {"systemCapabilitiesEnabled", "station-only"},
                      }},
            {"host0", {
                          {"remoteSysName", "enterprise"},
                          {"remotePortId", "vb-image2"},
                          {"remoteChassisId", "1efe5cecbfc248a09065ad6177a98b41"},
                          {"systemCapabilitiesEnabled", "station-only"},
                      }},
        };
    }

    auto lldp = std::make_shared<velia::system::LLDPDataProvider>([&]() { return json; });
    REQUIRE(lldp->getNeighbors() == expected);
}

#if LIST_NEIGHBORS_RUN
TEST_CASE("Real systemd")
{
    TEST_INIT_LOGS;

    auto dbusConnection = sdbus::createSystemBusConnection();
    auto lldp = std::make_shared<velia::system::LLDPDataProvider>([]() { return velia::utils::execAndWait(spdlog::get("system"), NETWORKCTL_EXECUTABLE, {"lldp", "--json=short"}, ""); });
    [[maybe_unused]] auto x = lldp->getNeighbors();
}
#endif
