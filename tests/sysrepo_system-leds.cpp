#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "fs-helpers/utils.h"
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

    auto fakeSysfsDir = std::filesystem::path {CMAKE_CURRENT_BINARY_DIR + "/tests/leds/"s};
    removeDirectoryTreeIfExists(fakeSysfsDir);
    std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/leds"s, fakeSysfsDir, std::filesystem::copy_options::recursive);

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

    std::shared_ptr<sysrepo::Vals> rpcInput = std::make_shared<sysrepo::Vals>(1);

    SECTION("UID led on")
    {
        rpcInput->val(0)->set("/czechlight-system:leds/uid/state", "on");
        auto res = client->rpc_send("/czechlight-system:leds/uid", rpcInput);
        REQUIRE(res->val_cnt() == 0);

        std::this_thread::sleep_for(10ms);
        REQUIRE(dataFromSysrepo(client, "/czechlight-system:leds", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/led[name='line:green']", ""},
                    {"/led[name='line:green']/brightness", "100"},
                    {"/led[name='line:green']/name", "line:green"},
                    {"/led[name='uid:blue']", ""},
                    {"/led[name='uid:blue']/brightness", "100"},
                    {"/led[name='uid:blue']/name", "uid:blue"},
                    {"/led[name='uid:green']", ""},
                    {"/led[name='uid:green']/brightness", "39"},
                    {"/led[name='uid:green']/name", "uid:green"},
                    {"/led[name='uid:red']", ""},
                    {"/led[name='uid:red']/brightness", "100"},
                    {"/led[name='uid:red']/name", "uid:red"},
                });
    }

    SECTION("UID led off")
    {
        rpcInput->val(0)->set("/czechlight-system:leds/uid/state", "off");
        auto res = client->rpc_send("/czechlight-system:leds/uid", rpcInput);
        REQUIRE(res->val_cnt() == 0);

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

    SECTION("UID led blinks")
    {
        rpcInput->val(0)->set("/czechlight-system:leds/uid/state", "blinking");
        auto res = client->rpc_send("/czechlight-system:leds/uid", rpcInput);
        REQUIRE(res->val_cnt() == 0);

        std::this_thread::sleep_for(10ms); // default timer trigger switches the LED on first
        REQUIRE(dataFromSysrepo(client, "/czechlight-system:leds", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/led[name='line:green']", ""},
                    {"/led[name='line:green']/brightness", "100"},
                    {"/led[name='line:green']/name", "line:green"},
                    {"/led[name='uid:blue']", ""},
                    {"/led[name='uid:blue']/brightness", "100"},
                    {"/led[name='uid:blue']/name", "uid:blue"},
                    {"/led[name='uid:green']", ""},
                    {"/led[name='uid:green']/brightness", "39"},
                    {"/led[name='uid:green']/name", "uid:green"},
                    {"/led[name='uid:red']", ""},
                    {"/led[name='uid:red']/brightness", "100"},
                    {"/led[name='uid:red']/name", "uid:red"},
                });
    }
}
