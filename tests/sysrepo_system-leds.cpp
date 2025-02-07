#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "fs-helpers/utils.h"
#include "pretty_printers.h"
#include "system/LED.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/sysrepo-helpers/common.h"
#include "utils/io.h"

using namespace std::literals;

TEST_CASE("Sysrepo reports system LEDs")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto fakeSysfsDir = std::filesystem::path {CMAKE_CURRENT_BINARY_DIR + "/tests/sysrepo_system-leds/"s};
    removeDirectoryTreeIfExists(fakeSysfsDir);
    std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/leds"s, fakeSysfsDir, std::filesystem::copy_options::recursive);

    const auto WAIT = 125ms /* poll interval */ + 100ms /* just to be sure */;

    velia::system::LED led(srConn, fakeSysfsDir);

    std::this_thread::sleep_for(WAIT);

    REQUIRE(dataFromSysrepo(client, "/czechlight-system:leds", sysrepo::Datastore::Operational) == std::map<std::string, std::string> {
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

    SECTION("Change uid:green brightness")
    {
        velia::utils::writeFile(fakeSysfsDir / "uid:green" / "brightness", "0");
        std::this_thread::sleep_for(WAIT);

        REQUIRE(dataFromSysrepo(client, "/czechlight-system:leds", sysrepo::Datastore::Operational) == std::map<std::string, std::string> {
                    {"/led[name='line:green']", ""},
                    {"/led[name='line:green']/brightness", "100"},
                    {"/led[name='line:green']/name", "line:green"},
                    {"/led[name='uid:blue']", ""},
                    {"/led[name='uid:blue']/brightness", "0"},
                    {"/led[name='uid:blue']/name", "uid:blue"},
                    {"/led[name='uid:green']", ""},
                    {"/led[name='uid:green']/brightness", "0"},
                    {"/led[name='uid:green']/name", "uid:green"},
                    {"/led[name='uid:red']", ""},
                    {"/led[name='uid:red']/brightness", "100"},
                    {"/led[name='uid:red']/name", "uid:red"},
                });
    }

    SECTION("Test UID RPC")
    {
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

        auto rpcInput = client.getContext().newPath("/czechlight-system:leds/uid/state", state);

        auto res = client.sendRPC(rpcInput);
        REQUIRE(!res);
        REQUIRE(velia::utils::readFileString(fakeSysfsDir / "uid:blue" / "trigger") == expectedTrigger);
        REQUIRE(velia::utils::readFileString(fakeSysfsDir / "uid:blue" / "brightness") == expectedBrightness);
    }
}
