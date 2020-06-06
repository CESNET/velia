#pragma once

#include "State.h"
#include "mux/Mux.h"

namespace cla {

class AbstractInput {
public:
    explicit AbstractInput(std::shared_ptr<MuxInputHandle> muxHandle);
    virtual ~AbstractInput() = 0;

protected:
    std::shared_ptr<MuxInputHandle> m_muxHandle;

    void changeState(State state);
};

}
