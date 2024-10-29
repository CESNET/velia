#include <sysrepo-cpp/utils/exception.hpp>
#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "pretty_printers.h"
#include "system/Firmware.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/sysrepo-helpers/common.h"
#include "tests/sysrepo-helpers/notifications.h"

using namespace std::literals;

std::vector<std::unique_ptr<trompeloeil::expectation>> expectationFactory(const DBusRAUCServer::InstallBehaviour& installType, NotificationWatcher& eventMock, trompeloeil::sequence& seq1)
{
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

    auto progressData = [](int progress, std::string message) {
        return Values{
            {"progress", std::to_string(progress)},
            {"message", message},
        };
    };

    if (installType == DBusRAUCServer::InstallBehaviour::OK) {
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(0, "Installing"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(0, "Determining slot states"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(20, "Determining slot states done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(20, "Checking bundle"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(20, "Veryfing signature"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(40, "Veryfing signature done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(40, "Checking bundle done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(40, "Loading manifest file"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(60, "Loading manifest file done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(60, "Determining target install group"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(80, "Determining target install group done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(80, "Updating slots"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(80, "Checking slot rootfs.0"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(85, "Checking slot rootfs.0 done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(85, "Copying image to rootfs.0"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(90, "Copying image to rootfs.0 done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(90, "Checking slot cfg.0"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(95, "Checking slot cfg.0 done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(95, "Copying image to cfg.0"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(100, "Copying image to cfg.0 done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(100, "Updating slots done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(100, "Installing done."))).IN_SEQUENCE(seq1));
    } else {
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(0, "Installing"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(0, "Determining slot states"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(20, "Determining slot states done."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(20, "Checking bundle"))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(40, "Checking bundle failed."))).IN_SEQUENCE(seq1));
        expectations.push_back(NAMED_REQUIRE_CALL(eventMock, notified(progressData(100, "Installing failed."))).IN_SEQUENCE(seq1));
    }

    return expectations;
}


TEST_CASE("Firmware in czechlight-system, RPC")
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

    REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware", sysrepo::Datastore::Operational) == std::map<std::string, std::string> {
                {"/firmware-slot[name='A']", ""},
                {"/firmware-slot[name='A']/is-healthy", "false"},
                {"/firmware-slot[name='A']/installed", "2021-01-13T17:15:50-00:00"},
                {"/firmware-slot[name='A']/name", "A"},
                {"/firmware-slot[name='A']/is-booted-now", "false"},
                {"/firmware-slot[name='A']/version", "v4-104-ge80fcd4"},
                {"/firmware-slot[name='A']/will-boot-next", "false"},
                {"/firmware-slot[name='B']", ""},
                {"/firmware-slot[name='B']/is-healthy", "true"},
                {"/firmware-slot[name='B']/installed", "2021-01-13T17:20:15-00:00"},
                {"/firmware-slot[name='B']/name", "B"},
                {"/firmware-slot[name='B']/is-booted-now", "true"},
                {"/firmware-slot[name='B']/version", "v4-103-g34d2f48"},
                {"/firmware-slot[name='B']/will-boot-next", "true"},
                {"/installation", ""},
                {"/installation/message", ""},
                {"/installation/status", "none"},
            });

    SECTION("Firmware install RPC")
    {
        auto rpcInput = client.getContext().newPath("/czechlight-system:firmware/installation/install/url", "/path/to/bundle/update.raucb");

        SECTION("Installation runs")
        {
            NotificationWatcher installProgressMock{client, "/czechlight-system:firmware/installation/update"};
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
            auto res = client.sendRPC(rpcInput);
            REQUIRE(res.child() == std::nullopt);

            std::this_thread::sleep_for(10ms); // lets wait a while, so the RAUC's callback for operation changed takes place
            REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", sysrepo::Datastore::Operational) == expectedInProgress);

            waitForCompletionAndBitMore(seq1); // wait for installation to complete
            REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", sysrepo::Datastore::Operational) == expectedFinished);
        }

        SECTION("Unsuccessfull install followed by successfull install")
        {
            NotificationWatcher installProgressMock{client, "/czechlight-system:firmware/installation/update"};
            // invoke unsuccessfull install
            {
                raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::FAILURE);
                auto progressExpectations = expectationFactory(DBusRAUCServer::InstallBehaviour::FAILURE, installProgressMock, seq1);
                client.sendRPC(rpcInput);

                waitForCompletionAndBitMore(seq1); // wait for installation to complete
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", sysrepo::Datastore::Operational) == std::map<std::string, std::string> {
                    {"/message", "Failed to download bundle https://10.88.3.11:8000/update.raucb: Transfer failed: error:1408F10B:SSL routines:ssl3_get_record:wrong version number"},
                    {"/status", "failed"},
                });
            }

            // invoke successfull install
            {
                raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::OK);
                auto progressExpectations = expectationFactory(DBusRAUCServer::InstallBehaviour::OK, installProgressMock, seq1);
                client.sendRPC(rpcInput);

                std::this_thread::sleep_for(10ms); // lets wait a while, so the RAUC's callback for operation changed takes place
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", sysrepo::Datastore::Operational) == std::map<std::string, std::string> {
                    {"/message", ""},
                    {"/status", "in-progress"},
                });

                waitForCompletionAndBitMore(seq1); // wait for installation to complete
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/installation", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/message", ""},
                    {"/status", "succeeded"},
                });
            }
        }

        SECTION("Installation in progress")
        {
            raucServer.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::OK);
            client.sendRPC(rpcInput);
            std::this_thread::sleep_for(10ms);

            SECTION("Invoking second installation throws")
            {
                REQUIRE_THROWS_WITH_AS(client.sendRPC(rpcInput),
                        "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                        // FIXME: why is this present twice? Looks like a libyang-v2.2/sysrepo change that I do not understand
                        " Already processing a different method (SR_ERR_OPERATION_FAILED)\n"
                        " Already processing a different method (SR_ERR_OPERATION_FAILED)\n"
                        " NETCONF: application: operation-failed: Already processing a different method",
                        sysrepo::ErrorWithCode);
            }

            SECTION("Firmware slot data are available")
            {
                // RAUC does not respond to GetSlotStatus when another operation in progress, so let's check we use the cached data
                REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware", sysrepo::Datastore::Operational) == std::map<std::string, std::string> {
                    {"/firmware-slot[name='A']", ""},
                    {"/firmware-slot[name='A']/is-healthy", "false"},
                    {"/firmware-slot[name='A']/installed", "2021-01-13T17:15:50-00:00"},
                    {"/firmware-slot[name='A']/name", "A"},
                    {"/firmware-slot[name='A']/is-booted-now", "false"},
                    {"/firmware-slot[name='A']/version", "v4-104-ge80fcd4"},
                    {"/firmware-slot[name='A']/will-boot-next", "false"},
                    {"/firmware-slot[name='B']", ""},
                    {"/firmware-slot[name='B']/is-healthy", "true"},
                    {"/firmware-slot[name='B']/installed", "2021-01-13T17:20:15-00:00"},
                    {"/firmware-slot[name='B']/name", "B"},
                    {"/firmware-slot[name='B']/is-booted-now", "true"},
                    {"/firmware-slot[name='B']/version", "v4-103-g34d2f48"},
                    {"/firmware-slot[name='B']/will-boot-next", "true"},
                    {"/installation", ""},
                    {"/installation/message", ""},
                    {"/installation/status", "in-progress"},
                });
            }
        }
    }

    SECTION("marking FW slots") {
        auto rpcInput = client.getContext().newPath("/czechlight-system:firmware/firmware-slot[name='A']/set-active-after-reboot");
        {
            REQUIRE_CALL(raucServer, impl_Mark("active", "rootfs.0")).IN_SEQUENCE(seq1);
            REQUIRE(client.sendRPC(rpcInput).child() == std::nullopt);
        }

        rpcInput = client.getContext().newPath("/czechlight-system:firmware/firmware-slot[name='B']/set-active-after-reboot");
        {
            REQUIRE_CALL(raucServer, impl_Mark("active", "rootfs.1")).IN_SEQUENCE(seq1);
            REQUIRE(client.sendRPC(rpcInput).child() == std::nullopt);
        }

        rpcInput = client.getContext().newPath("/czechlight-system:firmware/firmware-slot[name='B']/set-unhealthy");
        {
            REQUIRE_CALL(raucServer, impl_Mark("bad", "rootfs.1")).IN_SEQUENCE(seq1);
            REQUIRE(client.sendRPC(rpcInput).child() == std::nullopt);
        }

        waitForCompletionAndBitMore(seq1);
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
            {"[name='A']", ""},
            {"[name='A']/is-healthy", "false"},
            {"[name='A']/installed", "2021-01-13T17:15:50-00:00"},
            {"[name='A']/name", "A"},
            {"[name='A']/is-booted-now", "false"},
            {"[name='A']/version", "v4-104-ge80fcd4"},
            {"[name='A']/will-boot-next", "false"},
            {"[name='B']", ""},
            {"[name='B']/is-healthy", "true"},
            {"[name='B']/installed", "2021-01-13T17:20:15-00:00"},
            {"[name='B']/name", "B"},
            {"[name='B']/is-booted-now", "true"},
            {"[name='B']/version", "v4-103-g34d2f48"},
            {"[name='B']/will-boot-next", "true"},
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
            {"[name='A']", ""},
            {"[name='A']/is-healthy", "false"},
            {"[name='A']/name", "A"},
            {"[name='A']/is-booted-now", "false"},
            {"[name='A']/will-boot-next", "false"},
            {"[name='B']", ""},
            {"[name='B']/is-healthy", "true"},
            {"[name='B']/installed", "2021-01-13T17:20:15-00:00"},
            {"[name='B']/name", "B"},
            {"[name='B']/is-booted-now", "true"},
            {"[name='B']/version", "v4-103-g34d2f48"},
            {"[name='B']/will-boot-next", "true"},
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
            {"[name='B']", ""},
            {"[name='B']/is-healthy", "true"},
            {"[name='B']/installed", "2021-01-13T17:20:15-00:00"},
            {"[name='B']/name", "B"},
            {"[name='B']/is-booted-now", "true"},
            {"[name='B']/version", "v4-103-g34d2f48"},
            {"[name='B']/will-boot-next", "true"},
        };
    }

    auto raucServer = DBusRAUCServer(*dbusServerConnection, "rootfs.1", dbusRaucStatus);
    auto sysrepo = std::make_shared<velia::system::Firmware>(srConn, *dbusClientConnectionSignals, *dbusClientConnectionMethods);

    REQUIRE(dataFromSysrepo(client, "/czechlight-system:firmware/firmware-slot", sysrepo::Datastore::Operational) == expected);
}
