#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "fs-helpers/utils.h"
#include "pretty_printers.h"
#include "system/LED.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "utils/io.h"

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
    std::string state;
    std::string expectedTrigger;
    std::string expectedBrightness;

    /* This isn't what actually happens in real-life. The contents of the trigger file is usually something like this (i.e., list of available triggers).
     *
     *  [none] kbd-scrolllock kbd-numlock kbd-capslock kbd-kanalock kbd-shiftlock kbd-altgrlock kbd-ctrllock kbd-altlock kbd-shiftllock kbd-shiftrlock kbd-ctrlllock kbd-ctrlrlock mmc0 timer oneshot heartbeat gpio default-on transient panic netdev f1072004.mdio-mii:01:link f1072004.mdio-mii:01:1Gbps f1072004.mdio-mii:01:100Mbps f1072004.mdio-mii:01:10Mbps f1072004.mdio-mii:00:link f1072004.mdio-mii:00:1Gbps f1072004.mdio-mii:00:100Mbps f1072004.mdio-mii:00:10Mbps
     *
     * The value enclosed in brackets is the current active trigger. You can change it by writing a value corresponding to a trigger to the trigger file.
     * I'm not going to simulate sysfs led behaviour here, so just test that the original contents was "none" and the value written by the RPC is the expected value.
     * Also, I'm not implementing the 'timer' trigger behaviour, so the value written to the brightness file is static.
     */
    REQUIRE(velia::utils::readFileString(fakeSysfsDir / "uid:blue" / "trigger") == "none");

    SECTION("UID led on")
    {
        state = "on";
        expectedTrigger = "none";
        expectedBrightness = "256";
    }

    SECTION("UID led off")
    {
        state = "off";
        expectedTrigger = "none";
        expectedBrightness = "0";
    }

    SECTION("UID led blinking")
    {
        state = "blinking";
        expectedTrigger = "timer";
        expectedBrightness = "256";
    }

    rpcInput->val(0)->set("/czechlight-system:leds/uid/state", state.c_str());
    auto res = client->rpc_send("/czechlight-system:leds/uid", rpcInput);
    REQUIRE(res->val_cnt() == 0);
    REQUIRE(velia::utils::readFileString(fakeSysfsDir / "uid:blue" / "trigger") == expectedTrigger);
    REQUIRE(velia::utils::readFileString(fakeSysfsDir / "uid:blue" / "brightness") == expectedBrightness);
}
