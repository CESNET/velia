#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "ietf-system/RAUC.h"
#include "ietf-system/Sysrepo.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

using namespace std::literals;

TEST_CASE("ietf-system in Sysrepo")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;

    auto serverConnection = sdbus::createSessionBusConnection("de.pengutronix.rauc");
    auto clientConnection = sdbus::createSessionBusConnection();

    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    std::string primarySlot = "rootfs.0";
    std::map<std::string, velia::ietf_system::RAUC::SlotProperty> status = {
        {"rootfs.0", {
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
                     }}};

    auto server = DBusRAUCServer(*serverConnection, primarySlot, status);

    SECTION("Test system-state")
    {
        static const auto modulePrefix = "/ietf-system:system-state"s;
        auto rauc = std::make_shared<velia::ietf_system::RAUC>(*clientConnection);
        auto sysrepo = std::make_shared<velia::ietf_system::sysrepo::Sysrepo>(srSess, rauc);

        std::map<std::string, std::string> expected({
            {"/clock", ""},
            {"/platform", ""},
            {"/platform/os-name", "czechlight-clearfog"},
            {"/platform/os-release", "v4-101-ga9b541f"},
        });
        srSess->session_switch_ds(SR_DS_OPERATIONAL);
        REQUIRE(dataFromSysrepo(srSess, modulePrefix) == expected);
        srSess->session_switch_ds(SR_DS_RUNNING);
    }
}
