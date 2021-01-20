#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "pretty_printers.h"
#include "system/Sysrepo.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("System stuff in Sysrepo")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;

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
    auto rauc = std::make_shared<velia::system::RAUC>(*dbusClientConnection);

    SECTION("Test system-state")
    {
        TEST_SYSREPO_INIT_CLIENT;
        static const auto modulePrefix = "/ietf-system:system-state"s;

        SECTION("Valid data")
        {
            std::filesystem::path file;
            std::map<std::string, std::string> expected;

            SECTION("Real data")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release";
                expected = {
                    {"/clock", ""},
                    {"/platform", ""},
                    {"/platform/os-name", "CzechLight"},
                    {"/platform/os-release", "v4-105-g8294175-dirty"},
                    {"/platform/os-version", "v4-105-g8294175-dirty"},
                };
            }

            SECTION("Missing =")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-equal";
                expected = {
                    {"/clock", ""},
                    {"/platform", ""},
                    {"/platform/os-name", ""},
                    {"/platform/os-release", ""},
                    {"/platform/os-version", ""},
                };
            }

            SECTION("Empty values")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/empty-values";
                expected = {
                    {"/clock", ""},
                    {"/platform", ""},
                    {"/platform/os-name", ""},
                    {"/platform/os-release", ""},
                    {"/platform/os-version", ""},
                };
            }

            auto sysrepo = std::make_shared<velia::system::Sysrepo>(srConn, file, rauc);
            REQUIRE(dataFromSysrepo(client, modulePrefix, SR_DS_OPERATIONAL) == expected);
        }

        SECTION("Invalid data (missing VERSION and NAME keys)")
        {
            REQUIRE_THROWS_AS(std::make_shared<velia::system::Sysrepo>(srConn, CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-keys", rauc), std::out_of_range);
        }
    }

    SECTION("RAUC Install RPC")
    {
        TEST_SYSREPO_INIT_CLIENT;
        auto sysrepo = std::make_shared<velia::system::Sysrepo>(srConn, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release", rauc);

        auto input = std::make_shared<sysrepo::Vals>(1);
        input->val(0)->set("/czechlight-system:rauc-install/source", "/path/to/bundle/update.raucb");

        SECTION("Installation runs")
        {
            std::map<std::string, std::string> expectedAfterCall = {
                {"/installation", ""},
                {"/installation/in-progress", "true"},
            };
            std::map<std::string, std::string> expectedAfterCompleted;

            SECTION("Successfull install")
            {
                raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::OK);
                expectedAfterCompleted = {
                    {"/installation", ""},
                    {"/installation/in-progress", "false"},
                    {"/installation/return-value", "0"},
                };
            }

            SECTION("Unsuccessfull install")
            {
                raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::FAILURE);
                expectedAfterCompleted = {
                    {"/installation", ""},
                    {"/installation/in-progress", "false"},
                    {"/installation/last-error", "Failed to download bundle https://10.88.3.11:8000/update.raucb: Transfer failed: error:1408F10B:SSL routines:ssl3_get_record:wrong version number"},
                    {"/installation/return-value", "1"},
                };
            }

            auto res = client->rpc_send("/czechlight-system:rauc-install", input);
            REQUIRE(res->val_cnt() == 1);
            REQUIRE(res->val(0)->xpath() == "/czechlight-system:rauc-install/status"s);
            REQUIRE(res->val(0)->val_to_string() == "Installing");

            REQUIRE(dataFromSysrepo(client, "/czechlight-system:rauc", SR_DS_OPERATIONAL) == expectedAfterCall);

            // now the installation is in progress, we should expect tree to change soon
            std::this_thread::sleep_for(2s);

            REQUIRE(dataFromSysrepo(client, "/czechlight-system:rauc", SR_DS_OPERATIONAL) == expectedAfterCompleted);
        }

        SECTION("Another operation in progress")
        {
            raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::OK);

            auto res = client->rpc_send("/czechlight-system:rauc-install", input);
            REQUIRE(res->val_cnt() == 1);
            REQUIRE(res->val(0)->xpath() == "/czechlight-system:rauc-install/status"s);
            REQUIRE(res->val(0)->val_to_string() == "Installing");
        }
    }
}
