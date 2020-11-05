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

    auto mx = std::make_shared<velia::StateManager>();
    auto o1 = std::make_shared<FakeOutput>();
    mx->m_outputSignal.connect([&](const velia::State state) { o1->update(state); });

    SECTION("One input")
    {
        REQUIRE_STATE_OUTPUT(OK);
        auto i1 = std::make_shared<ManuallyInvokableInput>(mx);

        SECTION("Nothing happens")
        {
        }

        SECTION("State changes")
        {
            REQUIRE_STATE_OUTPUT(ERROR);
            i1->invokeChangeState(velia::State::ERROR);

            REQUIRE_STATE_OUTPUT(OK);
            i1->invokeChangeState(velia::State::OK);
        }
    }

    SECTION("Multiple inputs")
    {
        REQUIRE_STATE_OUTPUT(OK);
        auto i1 = std::make_shared<ManuallyInvokableInput>(mx);

        auto i2 = std::make_shared<ManuallyInvokableInput>(mx);

        SECTION("Nothing happens")
        {
        }

        SECTION("State changes")
        {
            i1->invokeChangeState(velia::State::OK); // [OK, OK]

            i2->invokeChangeState(velia::State::OK); // [OK, OK]

            {
                REQUIRE_STATE_OUTPUT(ERROR);
                i1->invokeChangeState(velia::State::ERROR); // [ERROR, OK]
            }

            {
                REQUIRE_STATE_OUTPUT(WARNING);
                i1->invokeChangeState(velia::State::WARNING); // [WARNING, OK]
            }

            i2->invokeChangeState(velia::State::OK); // [WARNING, OK]

            {
                REQUIRE_STATE_OUTPUT(OK);
                i1->invokeChangeState(velia::State::OK); // [OK, OK]
            }

            {
                REQUIRE_STATE_OUTPUT(ERROR);
                i2->invokeChangeState(velia::State::ERROR); // [OK, ERROR]
            }

            {
                REQUIRE_STATE_OUTPUT(WARNING);
                i2->invokeChangeState(velia::State::WARNING); // [OK, WARNING]
            }

            i1->invokeChangeState(velia::State::WARNING); // [WARNING, WARNING]

            {
                REQUIRE_STATE_OUTPUT(ERROR);
                i2->invokeChangeState(velia::State::ERROR); // [WARNING, ERROR]
            }

            {
                REQUIRE_STATE_OUTPUT(WARNING);
                i2->invokeChangeState(velia::State::WARNING); // [WARNING, WARNING]
            }

            i1->invokeChangeState(velia::State::OK); // [OK, WARNING]

            {
                REQUIRE_STATE_OUTPUT(OK);
                i2->invokeChangeState(velia::State::OK); // [OK, OK]
            }

            i1.reset(); // [OK]
            i2.reset(); // [], but no update because there's nothing new to report

            {
                REQUIRE_STATE_OUTPUT(OK);
                i1 = std::make_shared<ManuallyInvokableInput>(mx); // [OK]
            }

            i1.reset(); // []
        }
    }
}
