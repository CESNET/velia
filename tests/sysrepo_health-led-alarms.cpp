#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Enum.hpp>
#include <trompeloeil.hpp>
#include "dbus-helpers/dbus_rauc_server.h"
#include "fs-helpers/utils.h"
#include "health/State.h"
#include "health/outputs/AlarmsOutputs.h"
#include "pretty_printers.h"
#include "system/LED.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "utils/io.h"

using namespace std::literals;

struct FakeLedCallback {
    MAKE_CONST_MOCK1(call, void(velia::health::State));
};

const std::string alarmNode = "/ietf-alarms:alarms/alarm-list/alarm";

#define EXPECT_COLOUR(STATE) REQUIRE_CALL(fakeLeds, call(STATE)).IN_SEQUENCE(seq1)

#define SET_ALARM(RESOURCE, SEVERITY, IS_CLEARED)                                                                                                                     \
    srSess.setItem(alarmNode + "[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier=''][resource='" RESOURCE "']/perceived-severity", SEVERITY); \
    srSess.setItem(alarmNode + "[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier=''][resource='" RESOURCE "']/is-cleared", IS_CLEARED);


TEST_CASE("Sysrepo reports system LEDs")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    client.switchDatastore(sysrepo::Datastore::Operational);
    srSess.switchDatastore(sysrepo::Datastore::Operational);

    FakeLedCallback fakeLeds;

    SECTION("Start with OK")
    {
        EXPECT_COLOUR(velia::health::State::OK);
        SET_ALARM("unit1", "major", "true");
        SET_ALARM("unit2", "critical", "true");
        srSess.applyChanges();

        velia::health::AlarmsOutputs alarms(client, {[&](velia::health::State state) { fakeLeds.call(state); }});

        EXPECT_COLOUR(velia::health::State::ERROR);
        SET_ALARM("unit1", "major", "false");
        srSess.applyChanges();

        EXPECT_COLOUR(velia::health::State::ERROR);
        SET_ALARM("unit2", "major", "false");
        srSess.applyChanges();

        EXPECT_COLOUR(velia::health::State::ERROR);
        SET_ALARM("unit3", "major", "true");
        srSess.applyChanges();

        EXPECT_COLOUR(velia::health::State::OK);
        SET_ALARM("unit1", "major", "true");
        SET_ALARM("unit2", "major", "true");
        srSess.applyChanges();

        waitForCompletionAndBitMore(seq1);
    }

    SECTION("Start with ERROR")
    {
        EXPECT_COLOUR(velia::health::State::ERROR);
        SET_ALARM("unit1", "major", "false");
        srSess.applyChanges();

        velia::health::AlarmsOutputs alarms(client, {[&](velia::health::State state) { fakeLeds.call(state); }});

        EXPECT_COLOUR(velia::health::State::ERROR);
        SET_ALARM("unit1", "critical", "false");
        srSess.applyChanges();

        EXPECT_COLOUR(velia::health::State::OK);
        SET_ALARM("unit1", "major", "true");
        srSess.applyChanges();

        waitForCompletionAndBitMore(seq1);
    }
}
