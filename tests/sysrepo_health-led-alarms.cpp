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
#include "tests/configure.cmake.h"
#include "tests/sysrepo-helpers/common.h"
#include "utils/io.h"

using namespace std::literals;

#define EXPECT_COLOUR(STATE) expectations.emplace_back(NAMED_REQUIRE_CALL(fakeLeds, call(STATE)).IN_SEQUENCE(seq1));

struct FakeLedCallback {
    MAKE_CONST_MOCK1(call, void(velia::health::State));
};

void setSummary(sysrepo::Session sess, unsigned indeterminate, unsigned warning, unsigned minor, unsigned major, unsigned critical)
{
    static const auto cleared = 42;

    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='indeterminate']/total", std::to_string(cleared + indeterminate));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='indeterminate']/not-cleared", std::to_string(indeterminate));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='indeterminate']/cleared", std::to_string(cleared));

    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='warning']/total", std::to_string(cleared + warning));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='warning']/not-cleared", std::to_string(warning));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='warning']/cleared", std::to_string(cleared));

    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='minor']/total", std::to_string(cleared + minor));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='minor']/not-cleared", std::to_string(minor));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='minor']/cleared", std::to_string(cleared));

    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='major']/total", std::to_string(cleared + major));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='major']/not-cleared", std::to_string(major));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='major']/cleared", std::to_string(cleared));

    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='critical']/total", std::to_string(cleared + critical));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='critical']/not-cleared", std::to_string(critical));
    sess.setItem("/ietf-alarms:alarms/summary/alarm-summary[severity='critical']/cleared", std::to_string(cleared));

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

    FakeLedCallback fakeLeds;
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

    SECTION("Start with OK")
    {
        EXPECT_COLOUR(velia::health::State::OK);
        setSummary(srSess, 0, 0, 0, 0, 0);
    }

    SECTION("Start with WARNING")
    {
        EXPECT_COLOUR(velia::health::State::WARNING);
        setSummary(srSess, 0, 1, 0, 0, 0);
    }

    SECTION("Start with ERROR")
    {
        EXPECT_COLOUR(velia::health::State::ERROR);
        setSummary(srSess, 0, 0, 1, 0, 0);
    }

    velia::health::AlarmsOutputs alarms(client, {[&](velia::health::State state) { fakeLeds.call(state); }});

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 0, 0, 2, 0, 0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 0, 0, 0, 3, 0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 0, 0, 0, 0, 4);

    EXPECT_COLOUR(velia::health::State::WARNING);
    setSummary(srSess, 0, 5, 0, 0, 0);

    EXPECT_COLOUR(velia::health::State::WARNING);
    setSummary(srSess, 6, 0, 0, 0, 0);

    EXPECT_COLOUR(velia::health::State::OK);
    setSummary(srSess, 0, 0, 0, 0, 0);

    EXPECT_COLOUR(velia::health::State::WARNING);
    setSummary(srSess, 2, 5, 0, 0, 0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 2, 5, 3, 0, 0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 2, 5, 3, 4, 0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 2, 5, 3, 4, 1);

    EXPECT_COLOUR(velia::health::State::OK);
    setSummary(srSess, 0, 0, 0, 0, 0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 0, 1, 1, 0, 0);

    EXPECT_COLOUR(velia::health::State::OK);
    setSummary(srSess, 0, 0, 0, 0, 0);

    EXPECT_COLOUR(velia::health::State::ERROR);
    setSummary(srSess, 1, 0, 1, 0, 0);

    waitForCompletionAndBitMore(seq1);
}
