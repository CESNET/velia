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

const auto ietfAlarmsNumberOfAlarms = "/ietf-alarms:alarms/alarm-list/number-of-alarms";

#define EXPECT_COLOUR(STATE) REQUIRE_CALL(fakeLeds, call(STATE)).IN_SEQUENCE(seq1)
#define SET_ALARM_COUNT(N)                                               \
    srSess.setItem(ietfAlarmsNumberOfAlarms, std::to_string(N).c_str()); \
    srSess.applyChanges();

TEST_CASE("Sysrepo reports system LEDs")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    client.switchDatastore(sysrepo::Datastore::Operational);
    srSess.switchDatastore(sysrepo::Datastore::Operational);

    FakeLedCallback fakeLeds;

    EXPECT_COLOUR(velia::health::State::OK);
    SET_ALARM_COUNT(0);

    velia::health::AlarmsOutputs alarms(client, {[&](velia::health::State state) { fakeLeds.call(state); }});

    SET_ALARM_COUNT(0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    SET_ALARM_COUNT(1);

    EXPECT_COLOUR(velia::health::State::ERROR);
    SET_ALARM_COUNT(2);

    EXPECT_COLOUR(velia::health::State::OK);
    SET_ALARM_COUNT(0);

    waitForCompletionAndBitMore(seq1);
}
