/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "inputs/AbstractInput.h"
#include "manager/StateManager.h"
#include "outputs/AbstractOutput.h"
#include "test_log_setup.h"

namespace trompeloeil {
template <>
void print(std::ostream& os, const State& state)
{
    os << "State::";
    switch (state) {
    case State::ERROR:
        os << "ERROR";
        break;
    case State::WARNING:
        os << "WARNING";
        break;
    case State::OK:
        os << "OK";
        break;
    }
}
}

class FakeInput : public cla::AbstractInput {
public:
    using cla::AbstractInput::AbstractInput;

    void invokeChangeState(State s)
    {
        updateState(s);
    }
};

class FakeOutput : public cla::AbstractOutput {
public:
    MAKE_MOCK1(update, void(State), override);
};

TEST_CASE("State multiplexer")
{
    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    std::shared_ptr<cla::StateManager> mx = std::make_shared<cla::StateManager>();

    SECTION("One input")
    {
        auto o1 = std::make_shared<FakeOutput>();
        mx->registerOutput(o1);

        REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
        auto i1 = std::make_shared<FakeInput>(mx);

        SECTION("Nothing happens")
        {
        }

        SECTION("State changes")
        {
            REQUIRE_CALL(*o1, update(State::ERROR)).IN_SEQUENCE(seq1);
            i1->invokeChangeState(State::ERROR);

            REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
            i1->invokeChangeState(State::OK);
        }

        // i1 destructs, no input left so no observer update
        mx->unregisterOutput(o1);
    }

    SECTION("Multiple inputs")
    {
        auto o1 = std::make_shared<FakeOutput>();
        mx->registerOutput(o1);

        REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
        auto i1 = std::make_shared<FakeInput>(mx);

        REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
        auto i2 = std::make_shared<FakeInput>(mx);

        SECTION("Nothing happens")
        {
        }

        SECTION("State changes")
        {
            REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
            i1->invokeChangeState(State::OK); // [OK, OK]

            REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
            i2->invokeChangeState(State::OK); // [OK, OK]

            REQUIRE_CALL(*o1, update(State::ERROR)).IN_SEQUENCE(seq1);
            i1->invokeChangeState(State::ERROR); // [ERROR, OK]

            REQUIRE_CALL(*o1, update(State::WARNING)).IN_SEQUENCE(seq1);
            i1->invokeChangeState(State::WARNING); // [WARNING, OK]

            REQUIRE_CALL(*o1, update(State::WARNING)).IN_SEQUENCE(seq1);
            i2->invokeChangeState(State::OK); // [WARNING, OK]

            REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
            i1->invokeChangeState(State::OK); // [OK, OK]

            REQUIRE_CALL(*o1, update(State::ERROR)).IN_SEQUENCE(seq1);
            i2->invokeChangeState(State::ERROR); // [OK, ERROR]

            REQUIRE_CALL(*o1, update(State::WARNING)).IN_SEQUENCE(seq1);
            i2->invokeChangeState(State::WARNING); // [OK, WARNING]

            REQUIRE_CALL(*o1, update(State::WARNING)).IN_SEQUENCE(seq1);
            i1->invokeChangeState(State::WARNING); // [WARNING, WARNING]
        }

        mx->unregisterOutput(o1);
    }

    SECTION("(Un)registering (in/ou)tputs")
    {
        SECTION("First output then one input")
        {
            auto o1 = std::make_shared<FakeOutput>();
            mx->registerOutput(o1);

            {
                // registering input should notify output
                REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1);
                auto i1 = std::make_shared<FakeInput>(mx);

                // unregistering input should notify output unless it is the last input
            }
            mx->unregisterOutput(o1);
        }

        SECTION("First output then multiple inputs")
        {
            auto o1 = std::make_shared<FakeOutput>();
            mx->registerOutput(o1);

            REQUIRE_CALL(*o1, update(State::OK)).IN_SEQUENCE(seq1).TIMES(3); // twice for register, once for deregister
            auto i1 = std::make_shared<FakeInput>(mx);
            auto i2 = std::make_shared<FakeInput>(mx);
        }
    }
}