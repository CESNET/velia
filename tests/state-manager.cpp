/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "fake.h"
#include "manager/StateManager.h"
#include "test_log_setup.h"

TEST_CASE("State multiplexer")
{
    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    auto mx = std::make_shared<cla::StateManager>();

    auto o1 = std::make_shared<FakeOutputMock>();
    auto o1Proxy = FakeOutputProxy(o1);

    SECTION("One input")
    {
        mx->registerOutput(o1Proxy);

        REQUIRE_STATE_OUTPUT(OK);
        auto i1 = std::make_shared<ManuallyInvokableInput>(mx);

        SECTION("Nothing happens")
        {
        }

        SECTION("State changes")
        {
            REQUIRE_STATE_OUTPUT(ERROR);
            i1->invokeChangeState(State::ERROR);

            REQUIRE_STATE_OUTPUT(OK);
            i1->invokeChangeState(State::OK);
        }
    }

    SECTION("Multiple inputs")
    {
        mx->registerOutput(o1Proxy);

        REQUIRE_STATE_OUTPUT(OK);
        auto i1 = std::make_shared<ManuallyInvokableInput>(mx);

        REQUIRE_STATE_OUTPUT(OK);
        auto i2 = std::make_shared<ManuallyInvokableInput>(mx);

        SECTION("Nothing happens")
        {
        }

        SECTION("State changes")
        {
            REQUIRE_STATE_OUTPUT(OK);
            i1->invokeChangeState(State::OK); // [OK, OK]

            REQUIRE_STATE_OUTPUT(OK);
            i2->invokeChangeState(State::OK); // [OK, OK]

            REQUIRE_STATE_OUTPUT(ERROR);
            i1->invokeChangeState(State::ERROR); // [ERROR, OK]

            REQUIRE_STATE_OUTPUT(WARNING);
            i1->invokeChangeState(State::WARNING); // [WARNING, OK]

            REQUIRE_STATE_OUTPUT(WARNING);
            i2->invokeChangeState(State::OK); // [WARNING, OK]

            REQUIRE_STATE_OUTPUT(OK);
            i1->invokeChangeState(State::OK); // [OK, OK]

            REQUIRE_STATE_OUTPUT(ERROR);
            i2->invokeChangeState(State::ERROR); // [OK, ERROR]

            REQUIRE_STATE_OUTPUT(WARNING);
            i2->invokeChangeState(State::WARNING); // [OK, WARNING]

            REQUIRE_STATE_OUTPUT(WARNING);
            i1->invokeChangeState(State::WARNING); // [WARNING, WARNING]

            REQUIRE_STATE_OUTPUT(ERROR);
            i2->invokeChangeState(State::ERROR); // [WARNING, ERROR]

            REQUIRE_STATE_OUTPUT(WARNING);
            i2->invokeChangeState(State::WARNING); // [WARNING, WARNING]

            REQUIRE_STATE_OUTPUT(WARNING);
            i1->invokeChangeState(State::OK); // [OK, WARNING]

            REQUIRE_STATE_OUTPUT(OK);
            i2->invokeChangeState(State::OK); // [OK, OK]
        }

        // manually destroy one of the two inputs so we capture the output update call
        REQUIRE_STATE_OUTPUT(OK);
        i1.reset();

        // no output update is invoked upon destroying i2 because it is the last input
    }


    SECTION("Multiple outputs")
    {
        auto o2 = std::make_shared<FakeOutputMock>();
        auto o2Proxy = FakeOutputProxy(o2);

        mx->registerOutput(o1Proxy);
        mx->registerOutput(o2Proxy);

        SECTION("Both outputs gets notified")
        {
            REQUIRE_STATE_OUTPUT_AT(*o1, OK);
            REQUIRE_STATE_OUTPUT_AT(*o2, OK);
            auto i1 = std::make_shared<ManuallyInvokableInput>(mx);
        }
    }
}