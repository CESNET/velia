#pragma once

#include <memory>
#include "health/State.h"
#include "health/manager/StateManager.h"

namespace velia::health {

/** @brief Interface for manager's input source. Manager should be notified through updateState invocation */
class AbstractInput {
public:
    explicit AbstractInput(std::shared_ptr<AbstractManager> manager);
    virtual ~AbstractInput() = 0;

protected:
    void updateState(const State& state);

private:
    /** Associated manager */
    std::shared_ptr<AbstractManager> m_manager;
};
}
