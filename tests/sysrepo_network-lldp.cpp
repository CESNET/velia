#include "trompeloeil_doctest.h"
#include <mutex>
#include "tests/configure.cmake.h"
#include "network/LLDP.h"
#include "network/LLDPSysrepo.h"
#include "tests/pretty_printers.h"
#include "tests/sysrepo-helpers/common.h"
#include "tests/sysrepo-helpers/datastore.h"
#include "tests/test_log_setup.h"

using namespace std::chrono_literals;
using namespace std::string_literals;

TEST_CASE("LLDP neighbors in sysrepo")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    std::mutex mtx;
    std::string json = R"({"Neighbors": []})";
    auto setJson = [&](const std::string& newJson) {
        std::lock_guard lck(mtx);
        json = newJson;
    };

    auto lldp = velia::network::LLDPSysrepo(
        srConn,
        std::make_shared<velia::network::LLDPDataProvider>(
            [&]() {
                std::lock_guard lck(mtx);
                return json;
            },
            velia::network::LLDPDataProvider::LocalData{
                .chassisId = "abcdef0123456deadc0ffeebeefcafe1",
                .chassisSubtype = "local",
            }),
        50ms);

    client.switchDatastore(sysrepo::Datastore::Operational);
    REQUIRE(!client.getData("/czechlight-lldp:nbr-list/*"));
    REQUIRE(dataFromSysrepo(client, "/czechlight-lldp:local") == std::map<std::string, std::string>{
                {"/chassisId", "abcdef0123456deadc0ffeebeefcafe1"},
                {"/chassisSubtype", "local"},
            });

    trompeloeil::sequence seq;
    DatastoreWatcher watcher(client, "/czechlight-lldp:nbr-list");
    auto expectPush = [&](const std::string& newJson) {
        REQUIRE_CALL(watcher, change(trompeloeil::_)).IN_SEQUENCE(seq);
        setJson(newJson);
        waitForCompletionAndBitMore(seq);
    };
    auto expectNoPush = [&](const std::string& newJson) {
        setJson(newJson);
        waitForCompletionAndBitMore(seq);
    };

    // nothing changed
    expectNoPush(R"({"Neighbors": []})");

    // Single link, single neighbor
    expectPush(R"({"Neighbors": [{"InterfaceIndex": 2, "InterfaceName": "ve-image", "Neighbors": [{"SystemName": "image", "PortID": "host0", "ChassisID": "7062a9e41c924ac6942da39c56e6b820", "EnabledCapabilities": 128}]}]})");
    REQUIRE(dataFromSysrepo(client, "/czechlight-lldp:nbr-list") == std::map<std::string, std::string>{
                {"/neighbors[1]", ""},
                {"/neighbors[1]/ifName", "ve-image"},
                {"/neighbors[1]/remotePortId", "host0"},
                {"/neighbors[1]/remoteSysName", "image"},
                {"/neighbors[1]/remoteChassisId", "7062a9e41c924ac6942da39c56e6b820"},
                {"/neighbors[1]/systemCapabilitiesEnabled", "station-only"},
            });

    // Two links per one neighbor: a new neighbor appears and the existing one changes
    expectPush(R"({
  "Neighbors": [
    {"InterfaceName": "enp0s31f6", "InterfaceIndex": 42, "Neighbors": [{"SystemName": "sw-a1128-01.fit.cvut.cz", "PortID": "Gi3/0/7", "ChassisID": "00:b8:b3:e6:17:80", "EnabledCapabilities": 4}]},
    {"InterfaceName": "ve-image", "InterfaceIndex": 666, "Neighbors": [{"SystemName": "image", "PortID": "host0", "ChassisID": "8b90f96f448140fb9b5d9d68e86d052e", "EnabledCapabilities": 128}]}
  ]
}
)");
    const auto twoLinks = std::map<std::string, std::string>{
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
    REQUIRE(dataFromSysrepo(client, "/czechlight-lldp:nbr-list") == twoLinks);

    // garbage from networkd keeps the last known state
    expectNoPush("");
    REQUIRE(dataFromSysrepo(client, "/czechlight-lldp:nbr-list") == twoLinks);

    // multiple neighbors: all previous neighbors are replaced
    expectPush(R"({
  "Neighbors": [{
    "InterfaceName": "host0", "InterfaceIndex": 42, "Neighbors": [{
        "SystemName": "image", "PortID": "host0", "ChassisID": "1631331c24bb499bb644fcdf7c9fd467", "EnabledCapabilities": 128
    }, {
        "SystemName": "enterprise", "PortID": "vb-image2", "ChassisID": "1efe5cecbfc248a09065ad6177a98b41", "EnabledCapabilities": 128
    }]
  }]
}
)");
    REQUIRE(dataFromSysrepo(client, "/czechlight-lldp:nbr-list") == std::map<std::string, std::string>{
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
            });

    // all of them go away
    expectPush(R"({"Neighbors": []})");
    REQUIRE(!client.getData("/czechlight-lldp:nbr-list/*"));
}
