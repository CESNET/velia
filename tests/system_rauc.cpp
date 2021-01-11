#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "ietf-system/RAUC.h"
#include "pretty_printers.h"
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
    std::map<std::string, velia::ietf_system::RAUC::SlotStatus> status;

    SECTION("real data")
    {
        primarySlot = "rootfs.0";
        status = {
            {"rootfs.1", {
                             {"activated.count", uint32_t {33}},
                             {"activated.timestamp", "2021-01-06T09:12:18Z"},
                             {"bootname", "B"},
                             {"boot-status", "bad"},
                             {"bundle.compatible", "czechlight-clearfog"},
                             {"bundle.version", "v4-101-ga9b541f"},
                             {"class", "rootfs"},
                             {"device", "/dev/mmcblk0p3"},
                             {"installed.count", uint32_t {33}},
                             {"installed.timestamp", "2021-01-06T09:12:13Z"},
                             {"sha256", "03c190a852f0f54c7294ab0480a6949c107fae706da8589dec2b2a826d1e42f4"},
                             {"size", uint64_t {45549364}},
                             {"state", "inactive"},
                             {"status", "ok"},
                             {"type", "ext4"},
                         }},
            {"rootfs.0", {
                             {"installed.timestamp", "2020-12-18T14:38:40Z"},
                             {"size", uint64_t {45441316}},
                             {"sha256", "626273fe8f16faf15ea3cce55ffe43f3612fbc0c752cb8f27ccf7c623dcb68b2"},
                             {"bundle.compatible", "czechlight-clearfog"},
                             {"installed.count", uint32_t {35}},
                             {"bundle.version", "v4-101-ga9b541f"},
                             {"type", "ext4"},
                             {"bootname", "A"},
                             {"activated.count", uint32_t {35}},
                             {"class", "rootfs"},
                             {"activated.timestamp", "2020-12-18T14:38:44Z"},
                             {"boot-status", "good"},
                             {"mountpoint", "/"},
                             {"state", "booted"},
                             {"status", "ok"},
                             {"device", "/dev/mmcblk0p1"},
                         }},
            {"cfg.1", {
                          {"bundle.compatible", "czechlight-clearfog"},
                          {"parent", "rootfs.1"},
                          {"state", "inactive"},
                          {"size", uint64_t {108}},
                          {"sha256", "5ca1b6c461fc194055d52b181f57c63dc1d34c19d041f6395e6f6abc039692bb"},
                          {"class", "cfg"},
                          {"device", "/dev/mmcblk0p4"},
                          {"type", "ext4"},
                          {"status", "ok"},
                          {"bundle.version", "v4-101-ga9b541f"},
                          {"installed.timestamp", "2021-01-06T09:12:17Z"},
                          {"installed.count", uint32_t {33}},
                      }},
            {"cfg.0", {
                          {"bundle.compatible", "czechlight-clearfog"},
                          {"mountpoint", "/cfg"},
                          {"parent", "rootfs.0"},
                          {"state", "active"},
                          {"size", uint64_t {108}},
                          {"sha256", "5ca1b6c461fc194055d52b181f57c63dc1d34c19d041f6395e6f6abc039692bb"},
                          {"class", "cfg"},
                          {"device", "/dev/mmcblk0p2"},
                          {"type", "ext4"},
                          {"status", "ok"},
                          {"bundle.version", "v4-101-ga9b541f"},
                          {"installed.timestamp", "2020-12-18T14:38:44Z"},
                          {"installed.count", uint32_t {35}},
                      }},
        };
    }

    auto server = DBusRAUCServer(*serverConnection, primarySlot, status);
    auto rauc = std::make_shared<velia::ietf_system::RAUC>(*clientConnection);

    REQUIRE(rauc->getPrimary() == primarySlot);
    REQUIRE(rauc->getSlotStatus() == status);
}
