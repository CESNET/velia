#include "trompeloeil_doctest.h"
#include "system/LLDP.h"
#include "system_vars.h"
#include "tests/pretty_printers.h"
#include "tests/test_log_setup.h"

using namespace std::literals;

TEST_CASE("Parsing with the mock")
{
    TEST_INIT_LOGS;

    std::vector<velia::system::NeighborEntry> expected;
    std::string json;

    SECTION("LLDP active on a single link")
    {
        json = R"({"Neighbors": [{"InterfaceIndex": 2, "InterfaceName": "ve-image", "Neighbors": [{"SystemName": "image", "PortID": "host0", "ChassisID": "7062a9e41c924ac6942da39c56e6b820", "EnabledCapabilities": 128}]}]})";
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
        json = R"({"Neighbors": []})";
        expected = {};
    }

    SECTION("Two LLDP links")
    {
        json = R"({
  "Neighbors": [
    {"InterfaceName": "enp0s31f6", "InterfaceIndex": 42, "Neighbors": [{"SystemName": "sw-a1128-01.fit.cvut.cz", "PortID": "Gi3/0/7", "ChassisID": "00:b8:b3:e6:17:80", "EnabledCapabilities": 4}]},
    {"InterfaceName": "ve-image", "InterfaceIndex": 666, "Neighbors": [{"SystemName": "image", "PortID": "host0", "ChassisID": "8b90f96f448140fb9b5d9d68e86d052e", "EnabledCapabilities": 128}]}
  ]
}
)";
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
        json = R"({
  "Neighbors": [{
    "InterfaceName": "host0", "InterfaceIndex": 42, "Neighbors": [{
        "SystemName": "image", "PortID": "host0", "ChassisID": "1631331c24bb499bb644fcdf7c9fd467", "EnabledCapabilities": 128
    }, {
        "SystemName": "enterprise", "PortID": "vb-image2", "ChassisID": "1efe5cecbfc248a09065ad6177a98b41", "EnabledCapabilities": 128
    }]
  }]
}
)";

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

    auto lldp = std::make_shared<velia::system::LLDPDataProvider>(
        [&]() { return json; },
        velia::system::LLDPDataProvider::LocalData{.chassisId = "blabla", .chassisSubtype = "local"});
    REQUIRE(lldp->getNeighbors() == expected);
    REQUIRE(lldp->localProperties() == std::map<std::string, std::string>{{"chassisId", "blabla"}, {"chassisSubtype", "local"}});
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
