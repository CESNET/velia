#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include "pretty_printers.h"
#include "system/LLDP.h"
#include "system/LLDPCallback.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"

TEST_CASE("Sysrepo opsdata callback")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    std::map<std::string, std::string> expected;
    std::string json;

    SECTION("Single link, single neighbor")
    {
        json = R"({"ve-image": [{"neighbor": {"systemName": "image", "portId": "host0", "chassisId": "7062a9e41c924ac6942da39c56e6b820", "enabledCapabilities": "a"}}]})";
        expected = {
            {"/neighbors[1]", ""},
            {"/neighbors[1]/ifName", "ve-image"},
            {"/neighbors[1]/remotePortId", "host0"},
            {"/neighbors[1]/remoteSysName", "image"},
            {"/neighbors[1]/remoteChassisId", "7062a9e41c924ac6942da39c56e6b820"},
            {"/neighbors[1]/systemCapabilitiesEnabled", "station-only"},
        };
    }

    SECTION("Two links per one neighbor")
    {
        json = R"({
"enp0s31f6": [{"neighbor": {"systemName": "sw-a1128-01.fit.cvut.cz", "portId": "Gi3/0/7", "chassisId": "00:b8:b3:e6:17:80", "enabledCapabilities": "b"}}],
"ve-image":  [{"neighbor": {"systemName": "image", "portId": "host0", "chassisId": "8b90f96f448140fb9b5d9d68e86d052e", "enabledCapabilities": "a"}}]
        })";
        expected = {
            {"/neighbors[1]", ""},
            {"/neighbors[1]/ifName", "enp0s31f6"},
            {"/neighbors[1]/remoteSysName", "sw-a1128-01.fit.cvut.cz"},
            {"/neighbors[1]/remotePortId", "Gi3/0/7"},
            {"/neighbors[1]/remoteChassisId", "00:b8:b3:e6:17:80"},
            {"/neighbors[1]/systemCapabilitiesEnabled", "bridge"},
            {"/neighbors[2]", ""},
            {"/neighbors[2]/ifName", "ve-image"},
            {"/neighbors[2]/remoteSysName", "image"},
            {"/neighbors[2]/remotePortId", "host0"},
            {"/neighbors[2]/remoteChassisId", "8b90f96f448140fb9b5d9d68e86d052e"},
            {"/neighbors[2]/systemCapabilitiesEnabled", "station-only"},
        };
    }

    SECTION("Multiple neighbors")
    {
        json = R"({"host0": [
{"neighbor": {"systemName": "image", "portId": "host0", "chassisId": "1631331c24bb499bb644fcdf7c9fd467", "enabledCapabilities": "a"}},
{"neighbor": {"systemName": "enterprise", "portId": "vb-image2", "chassisId": "1efe5cecbfc248a09065ad6177a98b41", "enabledCapabilities": "a"}}
        ]})";

        expected = {
            {"/neighbors[1]", ""},
            {"/neighbors[1]/ifName", "host0"},
            {"/neighbors[1]/remoteChassisId", "1631331c24bb499bb644fcdf7c9fd467"},
            {"/neighbors[1]/remotePortId", "host0"},
            {"/neighbors[1]/remoteSysName", "image"},
            {"/neighbors[1]/systemCapabilitiesEnabled", "station-only"},
            {"/neighbors[2]", ""},
            {"/neighbors[2]/ifName", "host0"},
            {"/neighbors[2]/remoteChassisId", "1efe5cecbfc248a09065ad6177a98b41"},
            {"/neighbors[2]/remotePortId", "vb-image2"},
            {"/neighbors[2]/remoteSysName", "enterprise"},
            {"/neighbors[2]/systemCapabilitiesEnabled", "station-only"},
        };
    }

    auto lldp = std::make_shared<velia::system::LLDPDataProvider>([&]() { return json; });
    auto sub = srSess.onOperGet("czechlight-lldp", velia::system::LLDPCallback(lldp), "/czechlight-lldp:nbr-list");

    client.switchDatastore(sysrepo::Datastore::Operational);
    REQUIRE(dataFromSysrepo(client, "/czechlight-lldp:nbr-list") == expected);
}
