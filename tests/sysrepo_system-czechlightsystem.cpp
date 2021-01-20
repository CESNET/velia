#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "pretty_printers.h"
#include "system/CzechlightSystem.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("czechlight-system")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto dbusServerConnection = sdbus::createSessionBusConnection("de.pengutronix.rauc");
    auto dbusClientConnection = sdbus::createSessionBusConnection();
    dbusClientConnection->enterEventLoopAsync();
    dbusServerConnection->enterEventLoopAsync();

    std::map<std::string, velia::system::RAUC::SlotProperties> dbusRaucStatus = {
        {"rootfs.1", {
                         {"activated.count", uint32_t {39}},
                         {"activated.timestamp", "2021-01-13T17:20:18Z"},
                         {"bootname", "B"},
                         {"boot-status", "good"},
                         {"bundle.compatible", "czechlight-clearfog"},
                         {"bundle.version", "v4-103-g34d2f48"},
                         {"class", "rootfs"},
                         {"device", "/dev/mmcblk0p3"},
                         {"installed.count", uint32_t {39}},
                         {"installed.timestamp", "2021-01-13T17:20:15Z"},
                         {"mountpoint", "/"},
                         {"sha256", "07b30d065c7aad64d2006ce99fd339c929d3ca97b666fca4584b9ef726469fc4"},
                         {"size", uint64_t {45601892}},
                         {"state", "booted"},
                         {"status", "ok"},
                         {"type", "ext4"},
                     }},
        {"rootfs.0", {
                         {"activated.count", uint32_t {41}},
                         {"activated.timestamp", "2021-01-13T17:15:54Z"},
                         {"bootname", "A"},
                         {"boot-status", "bad"},
                         {"bundle.compatible", "czechlight-clearfog"},
                         {"bundle.version", "v4-104-ge80fcd4"},
                         {"class", "rootfs"},
                         {"device", "/dev/mmcblk0p1"},
                         {"installed.count", uint32_t {41}},
                         {"installed.timestamp", "2021-01-13T17:15:50Z"},
                         {"sha256", "6d81e8f341edd17c127811f7347c7e23d18c2fc25c0bdc29ac56999cc9c25629"},
                         {"size", uint64_t {45647664}},
                         {"state", "inactive"},
                         {"status", "ok"},
                         {"type", "ext4"},
                     }},
        {"cfg.1", {
                      {"bundle.compatible", "czechlight-clearfog"},
                      {"bundle.version", "v4-103-g34d2f48"},
                      {"class", "cfg"},
                      {"device", "/dev/mmcblk0p4"},
                      {"installed.count", uint32_t {39}},
                      {"installed.timestamp", "2021-01-13T17:20:18Z"},
                      {"mountpoint", "/cfg"},
                      {"parent", "rootfs.1"},
                      {"sha256", "5ca1b6c461fc194055d52b181f57c63dc1d34c19d041f6395e6f6abc039692bb"},
                      {"size", uint64_t {108}},
                      {"state", "active"},
                      {"status", "ok"},
                      {"type", "ext4"},
                  }},
        {"cfg.0", {
                      {"bundle.compatible", "czechlight-clearfog"},
                      {"bundle.version", "v4-104-ge80fcd4"},
                      {"class", "cfg"},
                      {"device", "/dev/mmcblk0p2"},
                      {"installed.count", uint32_t {41}},
                      {"installed.timestamp", "2021-01-13T17:15:54Z"},
                      {"parent", "rootfs.0"},
                      {"sha256", "5ca1b6c461fc194055d52b181f57c63dc1d34c19d041f6395e6f6abc039692bb"},
                      {"size", uint64_t {108}},
                      {"state", "inactive"},
                      {"status", "ok"},
                      {"type", "ext4"},
                  }},
    };
    auto raucServer = DBusRAUCServer(*dbusServerConnection, "rootfs.1", dbusRaucStatus);
    auto sysrepo = std::make_shared<velia::system::CzechlightSystem>(srConn, *dbusClientConnection);

    REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware", SR_DS_OPERATIONAL) == std::map<std::string, std::string>{
        {"/installation", ""},
        {"/installation/message", ""},
        {"/installation/status", "none"},
    });

    SECTION("Firmware install RPC")
    {
        auto rpcInput = std::make_shared<sysrepo::Vals>(1);
        rpcInput->val(0)->set("/czechlight-system:firmware/installation/install/url", "/path/to/bundle/update.raucb");

        SECTION("Installation runs")
        {
            DBusRAUCServer::InstallBehaviour installType;
            std::map<std::string, std::string> expectedFinished, expectedInProgress = {
                {"/installation", ""},
                {"/installation/message", ""},
                {"/installation/status", "in-progress"},
            };

            SECTION("Successfull install")
            {
                installType = DBusRAUCServer::InstallBehaviour::OK;
                expectedFinished = {
                    {"/installation", ""},
                    {"/installation/message", ""},
                    {"/installation/status", "succeeded"},
                };
            }

            SECTION("Unsuccessfull install")
            {
                installType = DBusRAUCServer::InstallBehaviour::FAILURE;
                expectedFinished = {
                    {"/installation", ""},
                    {"/installation/message", "Failed to download bundle https://10.88.3.11:8000/update.raucb: Transfer failed: error:1408F10B:SSL routines:ssl3_get_record:wrong version number"},
                    {"/installation/status", "failed"},
                };
            }

            raucServer.installBundleBehaviour(installType);
            auto res = client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput);
            REQUIRE(res->val_cnt() == 0);

            std::this_thread::sleep_for(10ms); // lets wait a while, so the RAUC's callback for operation changed takes place
            REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware", SR_DS_OPERATIONAL) == expectedInProgress);

            std::this_thread::sleep_for(2s); // lets wait a while, so the installation can finish
            REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware", SR_DS_OPERATIONAL) == expectedFinished);
        }

        SECTION("Invoke another installation before the first finishes")
        {
            client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput);
            std::this_thread::sleep_for(10ms);
            REQUIRE_THROWS_WITH_AS(client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput), "User callback failed", sysrepo::sysrepo_exception);
        }
    }
}
