/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "health/inputs/AbstractInput.h"
#include "health/manager/AbstractManager.h"

class ManuallyInvokableInput : public velia::health::AbstractInput {
public:
    using velia::health::AbstractInput::AbstractInput;

    void invokeChangeState(velia::health::State s)
    {
        updateState(s);
    }
};

struct FakeOutput {
public:
    MAKE_MOCK1(update, void(velia::health::State));
};

#define REQUIRE_STATE_OUTPUT(STATE) REQUIRE_CALL(*o1, update(velia::health::State::STATE)).IN_SEQUENCE(seq1)

class FakeManager : public velia::health::AbstractManager {
public:
    MAKE_MOCK2(updateState, void(void*, velia::health::State), override);
    MAKE_MOCK2(registerInput, void(void*, velia::health::State), override);
    MAKE_MOCK1(unregisterInput, void(void*), override);
};
