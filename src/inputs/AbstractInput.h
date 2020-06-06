#pragma once

#include <memory>
#include "State.h"
#include "manager/StateManager.h"

namespace cla {

class AbstractInput : public std::enable_shared_from_this<AbstractInput> {
public:
    explicit AbstractInput();
    virtual ~AbstractInput() = 0;
    void connectToManager(StateManager& manager);

protected:
    void changeState(State state);

private:
    /** Mux input */
    std::weak_ptr<StateManagerInput> m_muxInput;
};
}
