#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Enum.hpp>
#include <trompeloeil.hpp>
#include "dbus-helpers/dbus_rauc_server.h"
#include "health/State.h"
#include "health/SystemdUnits.h"
#include "health/outputs/AlarmsOutputs.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/dbus-helpers/dbus_systemd_server.h"
#include "utils/io.h"

using namespace std::literals;

struct FakeLedCallback {
    MAKE_CONST_MOCK1(call, void(velia::health::State));
};

#define EXPECT_COLOUR(STATE) REQUIRE_CALL(fakeLeds, call(STATE)).IN_SEQUENCE(seq1)

TEST_CASE("Test raising alarms lighting LEDs with real sysrepo-ietf-alarmsd server")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT_CLIENT;

    {
        TEST_SYSREPO_INIT;
        auto srSessLED = srConn.sessionStart();

        // Create and setup separate connections for both client and server to simulate real-world (systemd server and our client)
        auto clientConnection = sdbus::createSessionBusConnection();
        auto serverConnection = sdbus::createSessionBusConnection();
        clientConnection->enterEventLoopAsync();
        serverConnection->enterEventLoopAsync();
        DbusSystemdServer systemdServer(*serverConnection);

        client.switchDatastore(sysrepo::Datastore::Operational);
        srSess.switchDatastore(sysrepo::Datastore::Operational);
        srSessLED.switchDatastore(sysrepo::Datastore::Operational);

        systemdServer.createUnit(*serverConnection, "unit1.service", "/org/freedesktop/systemd1/unit/unit1", "active", "running");
        systemdServer.createUnit(*serverConnection, "unit2.service", "/org/freedesktop/systemd1/unit/unit2", "activating", "auto-restart");
        systemdServer.createUnit(*serverConnection, "unit3.service", "/org/freedesktop/systemd1/unit/unit3", "failed", "failed");

        FakeLedCallback fakeLeds;
        velia::health::SystemdUnits systemdAlarms(srSess, *clientConnection, serverConnection->getUniqueName(), "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit");

        std::this_thread::sleep_for(100ms); // let's wait for some time so the alarm-inventory is populated

        REQUIRE(dataFromSysrepo(client, "/ietf-alarms:alarms/alarm-inventory", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']", ""},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/alarm-type-id", "velia-alarms:systemd-unit-failure"},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/description", "The systemd service is considered in failed state."},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[1]", "unit1.service"},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[2]", "unit2.service"},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[3]", "unit3.service"},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/severity-level[1]", "critical"},
                    {"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/will-clear", "true"},
                });

        EXPECT_COLOUR(velia::health::State::ERROR);
        velia::health::AlarmsOutputs alarms(srSessLED, {[&](velia::health::State state) { fakeLeds.call(state); }});

        EXPECT_COLOUR(velia::health::State::ERROR);
        systemdServer.changeUnitState("/org/freedesktop/systemd1/unit/unit2", "active", "running");

        EXPECT_COLOUR(velia::health::State::ERROR);
        systemdServer.changeUnitState("/org/freedesktop/systemd1/unit/unit1", "activating", "auto-restart");

        EXPECT_COLOUR(velia::health::State::ERROR);
        systemdServer.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");

        EXPECT_COLOUR(velia::health::State::OK);
        systemdServer.changeUnitState("/org/freedesktop/systemd1/unit/unit1", "active", "running");

        EXPECT_COLOUR(velia::health::State::ERROR);
        systemdServer.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "failed", "failed");

        waitForCompletionAndBitMore(seq1);
    }

    REQUIRE(!client.getData("/ietf-alarms:alarms/alarm-inventory/*"));
}
