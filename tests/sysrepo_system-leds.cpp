#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "pretty_printers.h"
#include "system/LED.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("Sysrepo reports system LEDs")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto fakeSysfsDir = std::filesystem::path {CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/leds/"s};
    velia::system::LED led(srConn, fakeSysfsDir);

    std::this_thread::sleep_for(10ms);

    REQUIRE(dataFromSysrepo(client, "/czechlight-system:leds", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                {"/led[name='line:green']", ""},
                {"/led[name='line:green']/brightness", "100"},
                {"/led[name='line:green']/name", "line:green"},
                {"/led[name='uid:blue']", ""},
                {"/led[name='uid:blue']/brightness", "0"},
                {"/led[name='uid:blue']/name", "uid:blue"},
                {"/led[name='uid:green']", ""},
                {"/led[name='uid:green']/brightness", "39"},
                {"/led[name='uid:green']/name", "uid:green"},
                {"/led[name='uid:red']", ""},
                {"/led[name='uid:red']/brightness", "100"},
                {"/led[name='uid:red']/name", "uid:red"},
            });
}
