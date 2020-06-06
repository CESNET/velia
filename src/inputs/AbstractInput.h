#pragma once

#include "State.h"
#include "mux/Mux.h"

namespace cla {

class AbstractInput {
public:
    explicit AbstractInput(MuxInputHandle& muxInput);
    virtual ~AbstractInput() = 0;

protected:
    void changeState(State state);

private:
    /** Mux input */
    MuxInputHandle& m_muxInput;
};

}
