#include "trompeloeil_doctest.h"
#include <sdbus-c++/sdbus-c++.h>
#include <unistd.h>
#include "dbus-helpers/dbus_network1_server.h"
#include "pretty_printers.h"
#include "system/LLDP.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

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

    const auto serverBus = "local.pid" + std::to_string(getpid()) + ".org.freedesktop.network1";
    auto dbusServerConnection = sdbus::createSessionBusConnection(serverBus);
    dbusServerConnection->enterEventLoopAsync();

    std::vector<std::pair<int, std::string>> links;
    std::string dataDir;
    std::vector<velia::system::NeighborEntry> expected;

    SECTION("LLDP active on a single link")
    {
        links = {{1, "lo"}, {2, "enp0s25"}, {3, "wlp3s0"}, {4, "tun0"}, {5, "br-53662f640039"}, {6, "docker0"}, {7, "br-e78120c0adda"}, {8, "ve-image"}};
        dataDir = "single-link";
        expected = {
            {"ve-image", {
                             {"remoteSysName", "image"},
                             {"remotePortId", "host0"},
                             {"remoteChassisId", "7062a9e41c924ac6942da39c56e6b820"},
                             {"systemCapabilitiesSupported", "bridge router station-only"},
                             {"systemCapabilitiesEnabled", "station-only"},
                         }}};
    }

    SECTION("No LLDP enabled")
    {
        links = {{1, "lo"}, {2, "enp0s25"}, {3, "wlp3s0"}, {4, "tun0"}, {5, "br-53662f640039"}, {6, "docker0"}, {7, "br-e78120c0adda"}, {8, "ve-image"}};
        dataDir = "no-link";
        expected = {};
    }

    SECTION("Two LLDP links")
    {
        links = {{1, "lo"}, {2, "enp0s25"}, {3, "enp0s31f6"}, {4, "ve-image"}};
        dataDir = "two-links";
        expected = {
            {"enp0s31f6", {
                              {"remoteSysName", "sw-a1128-01.fit.cvut.cz"},
                              {"remotePortId", "Gi3/0/7"},
                              {"remoteChassisId", "00:b8:b3:e6:17:80"},
                              {"systemCapabilitiesSupported", "bridge router"},
                              {"systemCapabilitiesEnabled", "bridge"},
                          }},
            {"ve-image", {
                             {"remoteSysName", "image"},
                             {"remotePortId", "host0"},
                             {"remoteChassisId", "8b90f96f448140fb9b5d9d68e86d052e"},
                             {"systemCapabilitiesSupported", "bridge router station-only"},
                             {"systemCapabilitiesEnabled", "station-only"},
                         }},
        };
    }

    SECTION("Multiple neighbors on one interface")
    {
        links = {{1, "host0"}};
        dataDir = "multiple-neighbors";
        expected = {
            {"host0", {
                          {"remoteSysName", "image"},
                          {"remotePortId", "host0"},
                          {"remoteChassisId", "1631331c24bb499bb644fcdf7c9fd467"},
                          {"systemCapabilitiesSupported", "bridge router station-only"},
                          {"systemCapabilitiesEnabled", "station-only"},
                      }},
            {"host0", {
                          {"remoteSysName", "enterprise"},
                          {"remotePortId", "vb-image2"},
                          {"remoteChassisId", "1efe5cecbfc248a09065ad6177a98b41"},
                          {"systemCapabilitiesSupported", "bridge router station-only"},
                          {"systemCapabilitiesEnabled", "station-only"},
                      }},
        };
    }

    auto dbusServer = DbusServer(*dbusServerConnection);
    dbusServer.setLinks(links); // intentionally not mocking DbusMockServer::ListLinks but using explicit set/get pattern so I can avoid an unneccesary dependency on trompeloeil

    auto dbusClient = sdbus::createSessionBusConnection();
    auto lldp = std::make_shared<velia::system::LLDPDataProvider>(std::filesystem::path(CMAKE_CURRENT_SOURCE_DIR "/tests/lldp/"s) / dataDir, *dbusClient, serverBus);

    REQUIRE(lldp->getNeighbors() == expected);
}

#if LIST_NEIGHBORS_RUN
TEST_CASE("Real systemd")
{
    TEST_INIT_LOGS;

    auto dbusConnection = sdbus::createSystemBusConnection();
    auto lldp = std::make_shared<lldp::lldp::LLDPDataProvider>("/run/systemd/netif/lldp", *dbusConnection, "org.freedesktop.network1");
    [[maybe_unused]] auto x = lldp->getNeighbors();
}
#endif
