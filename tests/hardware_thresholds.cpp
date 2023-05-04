/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#include "trompeloeil_doctest.h"
#include "ietf-hardware/thresholds.h"

using namespace velia::ietf_hardware;
using Thrs = Thresholds<int16_t>;
using OneThr = OneThreshold<int16_t>;

class Transitions {
public:
    MAKE_MOCK1(record, void(State));
};

#define EVENTS_INIT            \
    trompeloeil::sequence seq; \
    Transitions eventLog;      \
    Watcher w(thr);


#define EXPECT_EVENT(E, STATE)          \
    {                                   \
        auto statusChange = E;          \
        REQUIRE(statusChange);          \
        REQUIRE(statusChange == STATE); \
    }

#define EXPECT_NONE(E)          \
    {                           \
        auto statusChange = E;  \
        REQUIRE(!statusChange); \
    }

TEST_CASE("just one threshold")
{
    Thrs thr;

    SECTION("critical-low ok -> failed -> ok")
    {
        thr.criticalLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(10), State::Normal);
        EXPECT_EVENT(w.update(-10), State::CriticalLow);
        EXPECT_EVENT(w.update(10), State::Normal);
    }

    SECTION("critical-low failed")
    {
        thr.criticalLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(-10), State::CriticalLow);
    }

    SECTION("warning-low ok")
    {
        thr.warningLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(10), State::Normal);
    }

    SECTION("warning-low failed -> ignoring")
    {
        thr.warningLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(-10), State::WarningLow);
        thr.warningLow.reset();
        EXPECT_EVENT(w.setThresholds(thr), State::Disabled);

        EXPECT_NONE(w.update(-20));
        EXPECT_NONE(w.update(-10));
        EXPECT_NONE(w.update(0));
        EXPECT_NONE(w.update(10));
        EXPECT_NONE(w.update(-10));
    }
}

TEST_CASE("state transitions")
{
    Thrs thr;
    thr.criticalLow = OneThr{10, 1};

    EVENTS_INIT

    EXPECT_EVENT(w.update(10), State::Normal);
    EXPECT_NONE(w.update(12));
    EXPECT_EVENT(w.update(8), State::CriticalLow);

    thr.warningHigh = OneThr{20, 1};
    EXPECT_EVENT(w.setThresholds(thr), State::CriticalLow);

    EXPECT_EVENT(w.setThresholds(thr), State::CriticalLow);
    EXPECT_EVENT(w.update(10), State::Normal);

    thr.warningLow = OneThr{13, 1};
    thr.criticalHigh = OneThr{30, 1};
    EXPECT_EVENT( w.setThresholds(thr), State::WarningLow);

    EXPECT_NONE(w.update(12));
}

TEST_CASE("hysteresis")
{
    Thrs thr;
    thr.criticalHigh = OneThr{40, 2};
    thr.warningHigh = OneThr{30, 2};
    thr.warningLow = OneThr{20, 2};
    thr.criticalLow = OneThr{10, 2};
    EVENTS_INIT

    EXPECT_EVENT(w.update(25), State::Normal);
    EXPECT_EVENT(w.update(31), State::WarningHigh);

    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(29));

    EXPECT_EVENT(w.update(41), State::CriticalHigh);
    EXPECT_EVENT(w.update(37), State::WarningHigh);

    EXPECT_NONE(w.update(38));
    EXPECT_NONE(w.update(39));
    EXPECT_NONE(w.update(40));
}
