#pragma once

#include "State.h"
#include "manager/StateManager.h"

namespace cla {

class AbstractInput {
public:
    explicit AbstractInput(StateManagerInputHandle& muxInput);
    virtual ~AbstractInput() = 0;

protected:
    void changeState(State state);

private:
    /** Mux input */
    StateManagerInputHandle& m_muxInput;
};
}
