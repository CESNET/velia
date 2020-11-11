/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "inputs/AbstractInput.h"
#include "manager/AbstractManager.h"

class ManuallyInvokableInput : public velia::AbstractInput {
public:
    using velia::AbstractInput::AbstractInput;

    void invokeChangeState(velia::State s)
    {
        updateState(s);
    }
};

struct FakeOutput {
public:
    MAKE_MOCK1(update, void(velia::State));
};

#define REQUIRE_STATE_OUTPUT(STATE) REQUIRE_CALL(*o1, update(velia::State::STATE)).IN_SEQUENCE(seq1)

class FakeManager : public velia::AbstractManager {
public:
    MAKE_MOCK2(updateState, void(void*, velia::State), override);
    MAKE_MOCK2(registerInput, void(void*, velia::State), override);
    MAKE_MOCK1(unregisterInput, void(void*), override);
};
