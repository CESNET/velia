#include "trompeloeil_doctest.h"
#include <sdbus-c++/sdbus-c++.h>
#include "pretty_printers.h"
#include "system/IETFSystem.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "tests/dbus-helpers/dbus_systemd_server.h"

using namespace std::literals;

TEST_CASE("Sysrepo ietf-system")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto dbusConnection = sdbus::createSessionBusConnection();
    auto dbusServerConnection = sdbus::createSessionBusConnection(); // unfortunately, org.freedesktop.systemd1 is a service in session bus as well
    dbusServerConnection->enterEventLoopAsync();

    auto dbusSystemdServer = std::make_shared<DbusSystemdServer>(*dbusServerConnection);

    SECTION("Test system-state")
    {
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

            auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess, *dbusConnection, dbusServerConnection->getUniqueName(), file);
            REQUIRE(dataFromSysrepo(client, modulePrefix, SR_DS_OPERATIONAL) == expected);
        }

        SECTION("Invalid data (missing VERSION and NAME keys)")
        {
            REQUIRE_THROWS_AS(std::make_shared<velia::system::IETFSystem>(srSess, *dbusConnection, dbusServerConnection->getUniqueName(), CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-keys"), std::out_of_range);
        }
    }

    SECTION("RPC system-reboot returns")
    {
        auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess, *dbusConnection, dbusServerConnection->getUniqueName(), CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release");

        auto rpcInput = std::make_shared<sysrepo::Vals>(0);
        auto res = client->rpc_send("/ietf-system:system-restart", rpcInput);
        REQUIRE(res->val_cnt() == 0);
    }
}
