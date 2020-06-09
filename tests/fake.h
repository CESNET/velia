/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "inputs/AbstractInput.h"
#include "manager/AbstractManager.h"

class ManuallyInvokableInput : public cla::AbstractInput {
public:
    using cla::AbstractInput::AbstractInput;

    void invokeChangeState(State s)
    {
        updateState(s);
    }
};

// Trompeloeil does not allow copying/moving mock objects, hence the indirection.
struct FakeOutputMock {
public:
    MAKE_MOCK1(update, void(State));
};

struct FakeOutputProxy {
    std::shared_ptr<FakeOutputMock> m_mock;

public:
    FakeOutputProxy(std::shared_ptr<FakeOutputMock> mock)
        : m_mock(std::move(mock))
    {
    }
    void operator()(State state) const { m_mock->update(state); }
};

class FakeManager : public cla::AbstractManager {
public:
    MAKE_MOCK2(updateState, void(void*, State), override);
    MAKE_MOCK2(registerInput, void(void*, State), override);
    MAKE_MOCK1(unregisterInput, void(void*), override);
    MAKE_MOCK1(registerOutput, void(std::function<void(State)>), override);
};

#define REQUIRE_STATE_OUTPUT_AT(OUTPUT, STATE) REQUIRE_CALL(OUTPUT, update(State::STATE)).IN_SEQUENCE(seq1)
#define REQUIRE_STATE_OUTPUT(STATE) REQUIRE_STATE_OUTPUT_AT(*o1, STATE)