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
    explicit FakeInput(cla::StateManagerInputHandle& muxHandle)
        : AbstractInput(muxHandle)
    {
    }

    void invokeChangeState(State s)
    {
        changeState(s);
    }
};

TEST_CASE("State multiplexer")
{
    TEST_INIT_LOGS;

    cla::StateManager mx;

    SECTION("One input")
    {
        std::shared_ptr<FakeInput> i1 = std::make_shared<FakeInput>(mx.createInput());

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
        std::shared_ptr<FakeInput> i1 = std::make_shared<FakeInput>(mx.createInput());
        std::shared_ptr<FakeInput> i2 = std::make_shared<FakeInput>(mx.createInput());
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
            std::shared_ptr<FakeInput> i1 = std::make_shared<FakeInput>(mx.createInput());
            REQUIRE(mx.getInputs().size() == 1);
            REQUIRE(mx.getOutput() == State::OK);

            i1->invokeChangeState(State::ERROR);
            REQUIRE(mx.getOutput() == State::ERROR);
        }

        // input is disconnected
        REQUIRE(mx.getInputs().size() == 0);
        REQUIRE(mx.getOutput() == State::OK);
    }
}
