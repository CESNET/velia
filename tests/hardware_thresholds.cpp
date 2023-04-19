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

namespace velia::ietf_hardware {
std::ostream& operator<<(std::ostream& s, const State state)
{
    switch (state) {
    case State::Initial:
        s << "Unknown";
        break;
    case State::Disabled:
        s << "Disabled";
        break;
    case State::CriticalLow:
        s << "CriticalLow";
        break;
    case State::WarningLow:
        s << "WarningLow";
        break;
    case State::Normal:
        s << "Normal";
        break;
    case State::WarningHigh:
        s << "WarningHigh";
        break;
    case State::CriticalHigh:
        s << "CriticalHigh";
        break;
    }
    return s;
}
}

class Transitions {
public:
    MAKE_MOCK1(record, void(State));
};

#define EVENTS_INIT            \
    trompeloeil::sequence seq; \
    Transitions eventLog;      \
    Watcher w(thr);            \
    w.changed.connect([&eventLog](const auto state) { eventLog.record(state); });

#define EXPECT_EVENT(E) \
    REQUIRE_CALL(eventLog, record(E)).IN_SEQUENCE(seq)

TEST_CASE("just one threshold")
{
    Thrs thr;

    SECTION("critical-low ok -> failed -> ok")
    {
        thr.criticalLow = OneThr{0, 1};
        EVENTS_INIT
        {
            EXPECT_EVENT(State::Normal);
            w.update(10);
        }
        {
            EXPECT_EVENT(State::CriticalLow);
            w.update(-10);
        }
        {
            EXPECT_EVENT(State::Normal);
            w.update(10);
        }
    }

    SECTION("critical-low failed")
    {
        thr.criticalLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(State::CriticalLow);
        w.update(-10);
    }

    SECTION("warning-low ok")
    {
        thr.warningLow = OneThr{0, 1};
        EVENTS_INIT
        EXPECT_EVENT(State::Normal);
        w.update(10);
    }

    SECTION("warning-low failed -> ignoring")
    {
        thr.warningLow = OneThr{0, 1};
        EVENTS_INIT
        {
            EXPECT_EVENT(State::WarningLow);
            w.update(-10);
        }
        thr.warningLow.reset();
        {
            EXPECT_EVENT(State::Disabled);
            w.setThresholds(thr);
        }
        w.update(-20);
        w.update(-10);
        w.update(0);
        w.update(10);
        w.update(-10);
    }
}

TEST_CASE("state transitions")
{
    Thrs thr;
    thr.criticalLow = OneThr{10, 1};

    EVENTS_INIT

    {
        EXPECT_EVENT(State::Normal);
        w.update(10);
    }
    w.update(12);
    {
        EXPECT_EVENT(State::CriticalLow);
        w.update(8);
    }

    {
        EXPECT_EVENT(State::CriticalLow);
        thr.warningHigh = OneThr{20, 1};
        w.setThresholds(thr);
    }

    {
        EXPECT_EVENT(State::CriticalLow);
        w.setThresholds(thr);
    }

    {
        EXPECT_EVENT(State::Normal);
        w.update(10);
    }

    {
        EXPECT_EVENT(State::WarningLow);
        thr.warningLow = OneThr{13, 1};
        thr.criticalHigh = OneThr{30, 1};
        w.setThresholds(thr);
    }
    {
        // no update now
        w.update(12);
    }
}

TEST_CASE("hysteresis")
{
    Thrs thr;
    thr.criticalHigh = OneThr{40, 2};
    thr.warningHigh = OneThr{30, 2};
    thr.warningLow = OneThr{20, 2};
    thr.criticalLow = OneThr{10, 2};
    EVENTS_INIT

    {
        EXPECT_EVENT(State::Normal);
        w.update(25);
    }

    {
        EXPECT_EVENT(State::WarningHigh);
        w.update(31);
    }
    w.update(31);
    w.update(31);
    w.update(29);
    w.update(29);
    w.update(29);
    w.update(31);
    w.update(29);
    w.update(31);
    w.update(29);

    {
        EXPECT_EVENT(State::CriticalHigh);
        w.update(41);
    }
    {
        EXPECT_EVENT(State::WarningHigh);
        w.update(37);
    }
    w.update(38);
    w.update(39);
    w.update(40);
}
