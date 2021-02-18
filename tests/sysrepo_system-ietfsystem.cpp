#include "trompeloeil_doctest.h"
#include "pretty_printers.h"
#include "system/IETFSystem.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("Sysrepo ietf-system")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

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
                    {"/os-name", "CzechLight"},
                    {"/os-release", "v4-105-g8294175-dirty"},
                    {"/os-version", "v4-105-g8294175-dirty"},
                };
            }

            SECTION("Missing =")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-equal";
                expected = {
                    {"/os-name", ""},
                    {"/os-release", ""},
                    {"/os-version", ""},
                };
            }

            SECTION("Empty values")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/empty-values";
                expected = {
                    {"/os-name", ""},
                    {"/os-release", ""},
                    {"/os-version", ""},
                };
            }

            auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess, file);
            REQUIRE(dataFromSysrepo(client, modulePrefix + "/platform", SR_DS_OPERATIONAL) == expected);
        }

        SECTION("Invalid data (missing VERSION and NAME keys)")
        {
            REQUIRE_THROWS_AS(std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-keys"), std::out_of_range);
        }
    }

    SECTION("dummy values")
    {
        auto sys = std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release");
        const char* xpath;

        SECTION("location") {
            xpath = "/ietf-system:system/location";
        }

        SECTION("contact") {
            xpath = "/ietf-system:system/contact";
        }

        client->session_switch_ds(SR_DS_OPERATIONAL);
        REQUIRE_THROWS_WITH(client->get_item(xpath), "Item not found");

        client->session_switch_ds(SR_DS_RUNNING);
        client->set_item_str(xpath, "lamparna");

        REQUIRE(!!client->get_item(xpath));
    }

    SECTION("clock")
    {
        auto sys = std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release");
        client->session_switch_ds(SR_DS_OPERATIONAL);
        REQUIRE(!!client->get_item("/ietf-system:system-state/clock/current-datetime"));
    }

#ifdef TEST_RPC_SYSTEM_REBOOT
    SECTION("RPC system-restart")
    {
        auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release");

        auto rpcInput = std::make_shared<sysrepo::Vals>(0);
        auto res = client->rpc_send("/ietf-system:system-restart", rpcInput);
        REQUIRE(res->val_cnt() == 0);
    }
#endif
}
