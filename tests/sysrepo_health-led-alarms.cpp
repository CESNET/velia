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

#define EXPECT_COLOUR(STATE) REQUIRE_CALL(fakeLeds, call(STATE)).IN_SEQUENCE(seq1)

struct FakeLedCallback {
    MAKE_CONST_MOCK1(call, void(velia::health::State));
};

struct AlarmCount {
    unsigned notCleared;
    unsigned cleared;
};

void setSummary(sysrepo::Session sess, const std::map<std::string, AlarmCount>& summary)
{
    for (const auto& [severity, alarmCount] : summary) {
        sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='" + severity + "']/total", std::to_string(alarmCount.notCleared + alarmCount.cleared));
        sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='" + severity + "']/not-cleared", std::to_string(alarmCount.notCleared));
        sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='" + severity + "']/cleared", std::to_string(alarmCount.cleared));
    }

    sess.applyChanges();
}

TEST_CASE("Sysrepo reports system LEDs")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    client.switchDatastore(sysrepo::Datastore::Operational);
    srSess.switchDatastore(sysrepo::Datastore::Operational);

    setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 0}}, {"minor", {0, 0}}, {"major", {0, 0}}, {"critical", {0, 0}}});

    FakeLedCallback fakeLeds;

    SECTION("Start with OK")
    {
        EXPECT_COLOUR(velia::health::State::OK);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 0}}, {"minor", {0, 0}}, {"major", {0, 0}}, {"critical", {0, 1}}});

        velia::health::AlarmsOutputs alarms(client, {[&](velia::health::State state) { fakeLeds.call(state); }});

        EXPECT_COLOUR(velia::health::State::ERROR);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 0}}, {"minor", {0, 0}}, {"major", {1, 0}}, {"critical", {0, 1}}});

        EXPECT_COLOUR(velia::health::State::ERROR);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 0}}, {"minor", {0, 0}}, {"major", {2, 0}}, {"critical", {0, 1}}});

        EXPECT_COLOUR(velia::health::State::ERROR);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 0}}, {"minor", {0, 0}}, {"major", {1, 1}}, {"critical", {0, 1}}});

        EXPECT_COLOUR(velia::health::State::OK);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 0}}, {"minor", {0, 0}}, {"major", {0, 2}}, {"critical", {0, 1}}});

        waitForCompletionAndBitMore(seq1);
    }

    SECTION("Start with not-OK")
    {
        EXPECT_COLOUR(velia::health::State::ERROR);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 1}}, {"minor", {0, 0}}, {"major", {0, 0}}, {"critical", {1, 0}}});

        velia::health::AlarmsOutputs alarms(client, {[&](velia::health::State state) { fakeLeds.call(state); }});

        EXPECT_COLOUR(velia::health::State::OK);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 0}}, {"minor", {0, 0}}, {"major", {0, 1}}, {"critical", {0, 1}}});

        EXPECT_COLOUR(velia::health::State::WARNING);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {1, 0}}, {"minor", {0, 0}}, {"major", {0, 0}}, {"critical", {0, 1}}});

        EXPECT_COLOUR(velia::health::State::OK);
        setSummary(srSess, {{"indeterminate", {0, 0}}, {"warning", {0, 1}}, {"minor", {0, 0}}, {"major", {0, 0}}, {"critical", {0, 1}}});

        waitForCompletionAndBitMore(seq1);
    }
}
