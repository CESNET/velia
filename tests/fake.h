/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "inputs/AbstractInput.h"

class ManuallyInvokableInput : public velia::AbstractInput {
public:
    using velia::AbstractInput::AbstractInput;

    void invokeChangeState(velia::State s)
    {
        updateState(s);
    }
};

// Trompeloeil does not allow copying/moving mock objects, hence the indirection.
struct FakeOutputMock {
public:
    MAKE_MOCK1(update, void(velia::State));
};

struct FakeOutputProxy {
    std::shared_ptr<FakeOutputMock> m_mock;

public:
    FakeOutputProxy(std::shared_ptr<FakeOutputMock> mock)
        : m_mock(std::move(mock))
    {
    }
    void operator()(velia::State state) const { m_mock->update(state); }
};


#define REQUIRE_STATE_OUTPUT_AT(OUTPUT, STATE) REQUIRE_CALL(OUTPUT, update(velia::State::STATE)).IN_SEQUENCE(seq1)
#define REQUIRE_STATE_OUTPUT(STATE) REQUIRE_STATE_OUTPUT_AT(*o1, STATE)