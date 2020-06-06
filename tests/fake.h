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

struct FakeOutput {
public:
    MAKE_MOCK1(update, void(velia::State));
};


#define REQUIRE_STATE_OUTPUT(STATE) REQUIRE_CALL(*o1, update(velia::State::STATE)).IN_SEQUENCE(seq1)
