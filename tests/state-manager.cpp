/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <inputs/AbstractInput.h>
#include <manager/StateManager.h>
#include "test_log_setup.h"

class FakeInput : public cla::AbstractInput {
public:
    void invokeChangeState(State s)
    {
        changeState(s);
    }
};

TEST_CASE("State multiplexer")
{
    TEST_INIT_LOGS;

    cla::StateManager mx;
    auto i1 = std::make_shared<FakeInput>();
    auto i2 = std::make_shared<FakeInput>();

    SECTION("One input")
    {
        i1->connectToManager(mx);

        SECTION("Nothing happens")
        {
            REQUIRE(mx.getOutput() == State::OK);
        }

        SECTION("State changes")
        {
            i1->invokeChangeState(State::OK);
            REQUIRE(mx.getOutput() == State::OK);

            i1->invokeChangeState(State::WARNING);
            REQUIRE(mx.getOutput() == State::WARNING);

            i1->invokeChangeState(State::ERROR);
            REQUIRE(mx.getOutput() == State::ERROR);

            i1->invokeChangeState(State::OK);
            REQUIRE(mx.getOutput() == State::OK);
        }
    }

    SECTION("Multiple inputs")
    {
        i1->connectToManager(mx);
        i2->connectToManager(mx);
        REQUIRE(mx.getInputs().size() == 2);

        SECTION("Nothing happens")
        {
            REQUIRE(mx.getOutput() == State::OK);
        }

        SECTION("State changes")
        {
            i1->invokeChangeState(State::OK);
            i2->invokeChangeState(State::OK);
            REQUIRE(mx.getOutput() == State::OK);

            i1->invokeChangeState(State::WARNING);
            REQUIRE(mx.getOutput() == State::WARNING);

            i2->invokeChangeState(State::OK);
            REQUIRE(mx.getOutput() == State::WARNING);

            i1->invokeChangeState(State::OK);
            REQUIRE(mx.getOutput() == State::OK);

            i2->invokeChangeState(State::ERROR);
            REQUIRE(mx.getOutput() == State::ERROR);

            i2->invokeChangeState(State::WARNING);
            REQUIRE(mx.getOutput() == State::WARNING);

            i2->invokeChangeState(State::OK);
            REQUIRE(mx.getOutput() == State::OK);
        }
    }

    SECTION("Register/destroy inputs")
    {
        REQUIRE(mx.getOutput() == State::OK);

        {
            auto iTemp = std::make_shared<FakeInput>();

            i1->connectToManager(mx);
            iTemp->connectToManager(mx);
            REQUIRE(mx.getInputs().size() == 2);
            REQUIRE(mx.getOutput() == State::OK);

            iTemp->invokeChangeState(State::ERROR);
            REQUIRE(mx.getOutput() == State::ERROR);
        }

        // TODO: iTemp still lives inside manager
        REQUIRE(mx.getInputs().size() == 2);
        REQUIRE(mx.getOutput() == State::ERROR);
    }
}
