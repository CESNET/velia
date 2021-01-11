#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "pretty_printers.h"
#include "system/RAUC.h"
#include "test_log_setup.h"

using namespace std::chrono_literals;

TEST_CASE("Fetch RAUC data over DBus")
{
    using namespace std::literals::chrono_literals;

    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    // setup separate connections for both client and server. Can be done using one connection only but this way it is more generic
    auto serverConnection = sdbus::createSessionBusConnection("de.pengutronix.rauc");
    auto clientConnection = sdbus::createSessionBusConnection();

    // enter client and servers event loops
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    std::string primarySlot;
    std::map<std::string, velia::system::RAUC::SlotProperties> status;

    SECTION("real data")
    {
        primarySlot = "rootfs.1";
        status = {
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
    }

    auto server = DBusRAUCServer(*serverConnection, primarySlot, status);
    auto rauc = std::make_shared<velia::system::RAUC>(*clientConnection);

    REQUIRE(rauc->primarySlot() == primarySlot);
    REQUIRE(rauc->slotStatus() == status);
}
