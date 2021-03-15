#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "pretty_printers.h"
#include "system/Firmware.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "tests/mock/sysrepo/events.h"

using namespace std::literals;

struct InstallProgressMock {
    MAKE_CONST_MOCK2(update, void(int32_t, const std::string&));
};

std::vector<std::unique_ptr<trompeloeil::expectation>> expectationFactory(const DBusRAUCServer::InstallBehaviour& installType, const InstallProgressMock& eventMock, trompeloeil::sequence& seq1)
{
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

    if (installType == DBusRAUCServer::InstallBehaviour::OK) {
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(0, "Installing")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(0, "Determining slot states")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(20, "Determining slot states done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(20, "Checking bundle")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(20, "Veryfing signature")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(40, "Veryfing signature done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(40, "Checking bundle done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(40, "Loading manifest file")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(60, "Loading manifest file done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(60, "Determining target install group")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(80, "Determining target install group done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(80, "Updating slots")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(80, "Checking slot rootfs.0")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(85, "Checking slot rootfs.0 done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(85, "Copying image to rootfs.0")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(90, "Copying image to rootfs.0 done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(90, "Checking slot cfg.0")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(95, "Checking slot cfg.0 done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(95, "Copying image to cfg.0")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(100, "Copying image to cfg.0 done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(100, "Updating slots done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(100, "Installing done.")).IN_SEQUENCE(seq1));
    } else {
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(0, "Installing")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(0, "Determining slot states")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(20, "Determining slot states done.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(20, "Checking bundle")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(40, "Checking bundle failed.")).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, update(100, "Installing failed.")).IN_SEQUENCE(seq1));
    }

    return expectations;
}


TEST_CASE("Firmware in czechlight-system, RPC")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    // process install notifications
    InstallProgressMock installProgressMock;
    EventWatcher events([&installProgressMock](const EventWatcher::Event& event) {
        installProgressMock.update(std::stoi(event.data.at("/czechlight-system:firmware/installation/update/progress")), event.data.at("/czechlight-system:firmware/installation/update/message"));
    });

    auto dbusServerConnection = sdbus::createSessionBusConnection("de.pengutronix.rauc");
    auto dbusClientConnectionSignals = sdbus::createSessionBusConnection();
    auto dbusClientConnectionMethods = sdbus::createSessionBusConnection();
    dbusClientConnectionSignals->enterEventLoopAsync();
    dbusClientConnectionMethods->enterEventLoopAsync();
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
    auto sysrepo = std::make_shared<velia::system::Firmware>(srConn, *dbusClientConnectionSignals, *dbusClientConnectionMethods);

    REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                {"/firmware-slot[name='A']", ""},
                {"/firmware-slot[name='A']/boot-status", "bad"},
                {"/firmware-slot[name='A']/installed", "2021-01-13T17:15:50Z"},
                {"/firmware-slot[name='A']/name", "A"},
                {"/firmware-slot[name='A']/state", "inactive"},
                {"/firmware-slot[name='A']/version", "v4-104-ge80fcd4"},
                {"/firmware-slot[name='B']", ""},
                {"/firmware-slot[name='B']/boot-status", "good"},
                {"/firmware-slot[name='B']/installed", "2021-01-13T17:20:15Z"},
                {"/firmware-slot[name='B']/name", "B"},
                {"/firmware-slot[name='B']/state", "booted"},
                {"/firmware-slot[name='B']/version", "v4-103-g34d2f48"},
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
            subscription->event_notif_subscribe("czechlight-system", events, "/czechlight-system:firmware/installation/update");

            DBusRAUCServer::InstallBehaviour installType;
            std::map<std::string, std::string> expectedFinished, expectedInProgress = {
                {"/message", ""},
                {"/status", "in-progress"},
            };

            SECTION("Successfull install")
            {
                installType = DBusRAUCServer::InstallBehaviour::OK;
                expectedFinished = {
                    {"/message", ""},
                    {"/status", "succeeded"},
                };
            }

            SECTION("Unsuccessfull install")
            {
                installType = DBusRAUCServer::InstallBehaviour::FAILURE;
                expectedFinished = {
                    {"/message", "Failed to download bundle https://10.88.3.11:8000/update.raucb: Transfer failed: error:1408F10B:SSL routines:ssl3_get_record:wrong version number"},
                    {"/status", "failed"},
                };
            }

            raucServer.installBundleBehaviour(installType);
            auto progressExpectations = expectationFactory(installType, installProgressMock, seq1);
            auto res = client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput);
            REQUIRE(res->val_cnt() == 0);

            std::this_thread::sleep_for(10ms); // lets wait a while, so the RAUC's callback for operation changed takes place
            REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", SR_DS_OPERATIONAL) == expectedInProgress);

            waitForCompletionAndBitMore(seq1); // wait for installation to complete
            REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", SR_DS_OPERATIONAL) == expectedFinished);
        }

        SECTION("Unsuccessfull install followed by successfull install")
        {
            subscription->event_notif_subscribe("czechlight-system", events, "/czechlight-system:firmware/installation/update");

            // invoke unsuccessfull install
            {
                raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::FAILURE);
                auto progressExpectations = expectationFactory(DBusRAUCServer::InstallBehaviour::FAILURE, installProgressMock, seq1);
                client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput);

                waitForCompletionAndBitMore(seq1); // wait for installation to complete
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/message", "Failed to download bundle https://10.88.3.11:8000/update.raucb: Transfer failed: error:1408F10B:SSL routines:ssl3_get_record:wrong version number"},
                    {"/status", "failed"},
                });
            }

            // invoke successfull install
            {
                raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::OK);
                auto progressExpectations = expectationFactory(DBusRAUCServer::InstallBehaviour::OK, installProgressMock, seq1);
                client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput);

                std::this_thread::sleep_for(10ms); // lets wait a while, so the RAUC's callback for operation changed takes place
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/message", ""},
                    {"/status", "in-progress"},
                });

                waitForCompletionAndBitMore(seq1); // wait for installation to complete
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", SR_DS_OPERATIONAL) == std::map<std::string, std::string>{
                    {"/message", ""},
                    {"/status", "succeeded"},
                });
            }
        }

        SECTION("Installation in progress")
        {
            raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::OK);
            client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput);
            std::this_thread::sleep_for(10ms);

            SECTION("Invoking second installation throws")
            {
                REQUIRE_THROWS_WITH_AS(client->rpc_send("/czechlight-system:firmware/installation/install", rpcInput), "User callback failed", sysrepo::sysrepo_exception);
            }

            SECTION("Firmware slot data are available")
            {
                // RAUC does not respond to GetSlotStatus when another operation in progress, so let's check we use the cached data
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/firmware-slot[name='A']", ""},
                    {"/firmware-slot[name='A']/boot-status", "bad"},
                    {"/firmware-slot[name='A']/installed", "2021-01-13T17:15:50Z"},
                    {"/firmware-slot[name='A']/name", "A"},
                    {"/firmware-slot[name='A']/state", "inactive"},
                    {"/firmware-slot[name='A']/version", "v4-104-ge80fcd4"},
                    {"/firmware-slot[name='B']", ""},
                    {"/firmware-slot[name='B']/boot-status", "good"},
                    {"/firmware-slot[name='B']/installed", "2021-01-13T17:20:15Z"},
                    {"/firmware-slot[name='B']/name", "B"},
                    {"/firmware-slot[name='B']/state", "booted"},
                    {"/firmware-slot[name='B']/version", "v4-103-g34d2f48"},
                    {"/installation", ""},
                    {"/installation/message", ""},
                    {"/installation/status", "in-progress"},
                });
            }
        }
    }
}

TEST_CASE("Firmware in czechlight-system, slot status")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto dbusServerConnection = sdbus::createSessionBusConnection("de.pengutronix.rauc");
    auto dbusClientConnectionSignals = sdbus::createSessionBusConnection();
    auto dbusClientConnectionMethods = sdbus::createSessionBusConnection();
    dbusClientConnectionSignals->enterEventLoopAsync();
    dbusClientConnectionMethods->enterEventLoopAsync();
    dbusServerConnection->enterEventLoopAsync();

    std::map<std::string, velia::system::RAUC::SlotProperties> dbusRaucStatus;
    std::map<std::string, std::string> expected;

    SECTION("Complete data")
    {
        dbusRaucStatus = {
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

        expected = {
            {"[name='A']/boot-status", "bad"},
            {"[name='A']/installed", "2021-01-13T17:15:50Z"},
            {"[name='A']/name", "A"},
            {"[name='A']/state", "inactive"},
            {"[name='A']/version", "v4-104-ge80fcd4"},
            {"[name='B']/boot-status", "good"},
            {"[name='B']/installed", "2021-01-13T17:20:15Z"},
            {"[name='B']/name", "B"},
            {"[name='B']/state", "booted"},
            {"[name='B']/version", "v4-103-g34d2f48"},
        };
    }

    SECTION("Missing data in rootfs.0")
    {
        dbusRaucStatus = {
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
                             {"bootname", "A"},
                             {"boot-status", "bad"},
                             {"class", "rootfs"},
                             {"device", "/dev/mmcblk0p1"},
                             {"sha256", "6d81e8f341edd17c127811f7347c7e23d18c2fc25c0bdc29ac56999cc9c25629"},
                             {"size", uint64_t {45647664}},
                             {"state", "inactive"},
                             {"status", "ok"},
                             {"type", "ext4"},
                         }},
        };

        expected = {
            {"[name='A']/boot-status", "bad"},
            {"[name='A']/name", "A"},
            {"[name='A']/state", "inactive"},
            {"[name='B']/boot-status", "good"},
            {"[name='B']/installed", "2021-01-13T17:20:15Z"},
            {"[name='B']/name", "B"},
            {"[name='B']/state", "booted"},
            {"[name='B']/version", "v4-103-g34d2f48"},
        };
    }

    SECTION("Missing bootname in rootfs.0")
    {
        dbusRaucStatus = {
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
                             {"boot-status", "bad"},
                             {"class", "rootfs"},
                             {"device", "/dev/mmcblk0p1"},
                             {"sha256", "6d81e8f341edd17c127811f7347c7e23d18c2fc25c0bdc29ac56999cc9c25629"},
                             {"size", uint64_t {45647664}},
                             {"state", "inactive"},
                             {"status", "ok"},
                             {"type", "ext4"},
                         }},
        };

        expected = {
            {"[name='B']/boot-status", "good"},
            {"[name='B']/installed", "2021-01-13T17:20:15Z"},
            {"[name='B']/name", "B"},
            {"[name='B']/state", "booted"},
            {"[name='B']/version", "v4-103-g34d2f48"},
        };
    }

    auto raucServer = DBusRAUCServer(*dbusServerConnection, "rootfs.1", dbusRaucStatus);
    auto sysrepo = std::make_shared<velia::system::Firmware>(srConn, *dbusClientConnectionSignals, *dbusClientConnectionMethods);

    REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/firmware-slot", SR_DS_OPERATIONAL) == expected);
}
