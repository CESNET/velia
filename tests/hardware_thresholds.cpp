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

#define EVENTS_INIT \
    trompeloeil::sequence seq; \
    Transitions eventLog; \
    Watcher w(thr);


#define EXPECT_EVENT(E, STATE, THRESHOLD_VALUE) \
    { \
        auto statusChange = E; \
        REQUIRE(statusChange); \
        REQUIRE(statusChange->newState == STATE); \
        REQUIRE(statusChange->exceededThresholdValue == THRESHOLD_VALUE); \
    }

#define EXPECT_NONE(E) \
    { \
        auto statusChange = E; \
        REQUIRE(!statusChange); \
    }

TEST_CASE("just one threshold")
{
    Thrs thr;

    SECTION("critical-low ok -> failed -> ok")
    {
        thr.criticalLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(10), State::Normal, std::nullopt);
        EXPECT_EVENT(w.update(-10), State::CriticalLow, 0);
        EXPECT_EVENT(w.update(10), State::Normal, std::nullopt);
    }

    SECTION("critical-low failed")
    {
        thr.criticalLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(-10), State::CriticalLow, 0);
    }

    SECTION("warning-low ok")
    {
        thr.warningLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(10), State::Normal, std::nullopt);
    }

    SECTION("warning-low failed -> ignoring")
    {
        thr.warningLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(w.update(-10), State::WarningLow, 0);
        thr.warningLow.reset();
        EXPECT_EVENT(w.setThresholds(thr), State::Disabled, std::nullopt);

        EXPECT_NONE(w.update(-20));
        EXPECT_NONE(w.update(-10));
        EXPECT_NONE(w.update(0));
        EXPECT_NONE(w.update(10));
        EXPECT_NONE(w.update(-10));
    }

    SECTION("setting thresholds before updating first value does not trigger any events")
    {
        EVENTS_INIT;
        thr.criticalLow = OneThr{0, 1};
        EXPECT_NONE(w.setThresholds(thr));
    }
}

TEST_CASE("state transitions")
{
    Thrs thr;
    thr.criticalLow = OneThr{10, 1};

    EVENTS_INIT

    EXPECT_EVENT(w.update(10), State::Normal, std::nullopt);
    EXPECT_NONE(w.update(12));
    EXPECT_EVENT(w.update(8), State::CriticalLow, 10);

    EXPECT_EVENT(w.update(std::nullopt), State::NoValue, std::nullopt);
    EXPECT_NONE(w.update(std::nullopt));
    EXPECT_EVENT(w.update(10), State::Normal, std::nullopt);
    EXPECT_EVENT(w.update(std::nullopt), State::NoValue, std::nullopt);
    EXPECT_EVENT(w.update(6), State::CriticalLow, 10);

    thr.warningHigh = OneThr{20, 1};
    EXPECT_EVENT(w.setThresholds(thr), State::CriticalLow, 10);

    EXPECT_EVENT(w.setThresholds(thr), State::CriticalLow, 10);
    EXPECT_EVENT(w.update(10), State::Normal, std::nullopt);

    thr.warningLow = OneThr{13, 1};
    thr.criticalHigh = OneThr{30, 1};
    EXPECT_EVENT(w.setThresholds(thr), State::WarningLow, 13);

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

    EXPECT_EVENT(w.update(25), State::Normal, std::nullopt);
    EXPECT_EVENT(w.update(31), State::WarningHigh, 30);

    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(29));
    EXPECT_NONE(w.update(31));
    EXPECT_NONE(w.update(29));

    EXPECT_EVENT(w.update(41), State::CriticalHigh, 40);
    EXPECT_EVENT(w.update(37), State::WarningHigh, 30);

    EXPECT_NONE(w.update(38));
    EXPECT_NONE(w.update(39));
    EXPECT_NONE(w.update(40));

    EXPECT_EVENT(w.update(41), State::CriticalHigh, 40);
    EXPECT_NONE(w.update(39));
    EXPECT_EVENT(w.update(std::nullopt), State::NoValue, std::nullopt);
    EXPECT_EVENT(w.update(41), State::CriticalHigh, 40);
    EXPECT_EVENT(w.update(std::nullopt), State::NoValue, std::nullopt);
    EXPECT_EVENT(w.update(39), State::WarningHigh, 30);
    EXPECT_EVENT(w.update(std::nullopt), State::NoValue, std::nullopt);

    thr.criticalHigh.reset();
    EXPECT_NONE(w.setThresholds(thr));

    thr.criticalHigh = OneThr{40, 2};
    EXPECT_NONE(w.setThresholds(thr));
    EXPECT_EVENT(w.update(41), State::CriticalHigh, 40);
}
