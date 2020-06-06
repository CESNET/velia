/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <inputs/AbstractInput.h>
#include <mux/Mux.h>
#include <outputs/AbstractOutput.h>
#include "test_log_setup.h"

class FakeInput : public cla::AbstractInput {
public:
    explicit FakeInput(cla::MuxInputHandle& muxHandle)
        : AbstractInput(muxHandle)
    {
    }

    void invokeChangeState(State s)
    {
        changeState(s);
    }
};

class FakeOutput : public cla::AbstractOutput {
    State m_state;

public:
    explicit FakeOutput() = default;

    void update(const State state) override
    {
        m_state = state;
    }

    State getState() const
    {
        return m_state;
    }
};

TEST_CASE("State multiplexer")
{
    TEST_INIT_LOGS;

    cla::Mux mx;

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

    SECTION("Mux notifies outputs")
    {
        std::shared_ptr<FakeInput> i1 = std::make_shared<FakeInput>(mx.createInput());
        std::shared_ptr<FakeInput> i2 = std::make_shared<FakeInput>(mx.createInput());
        std::shared_ptr<FakeOutput> o1 = std::make_shared<FakeOutput>();
        std::shared_ptr<FakeOutput> o2 = std::make_shared<FakeOutput>();

        mx.registerOutput(o1);

        i1->invokeChangeState(State::OK);
        i2->invokeChangeState(State::OK);
        REQUIRE(o1->getState() == State::OK);

        i2->invokeChangeState(State::ERROR);
        mx.registerOutput(o2);
        REQUIRE(o1->getState() == State::ERROR);
        REQUIRE(o2->getState() == State::ERROR);

        i2->invokeChangeState(State::ERROR);
        REQUIRE(o1->getState() == State::ERROR);
        REQUIRE(o2->getState() == State::ERROR);
    }
}
