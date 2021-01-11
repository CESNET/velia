#include "trompeloeil_doctest.h"
#include "pretty_printers.h"
#include "system/sysrepo/Sysrepo.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("System stuff in Sysrepo")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;

    SECTION("Test system-state")
    {
        TEST_SYSREPO_INIT_CLIENT;
        static const auto modulePrefix = "/ietf-system:system-state"s;

        SECTION("Real data")
        {
            auto sysrepo = std::make_shared<velia::system::sysrepo::Sysrepo>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release");

            std::map<std::string, std::string> expected({
                {"/clock", ""},
                {"/platform", ""},
                {"/platform/os-name", "CzechLight"},
                {"/platform/os-release", "v4-105-g8294175-dirty"},
                {"/platform/os-version", "v4-105-g8294175-dirty"},
            });

            REQUIRE(dataFromSysrepo(srCliSess, modulePrefix, SR_DS_OPERATIONAL) == expected);
        }

        SECTION("Invalid data (missing VERSION and NAME keys)")
        {
            REQUIRE_THROWS_AS(std::make_shared<velia::system::sysrepo::Sysrepo>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-keys"), std::out_of_range);
        }
    }
}
