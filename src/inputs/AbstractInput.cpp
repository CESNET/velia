#include "AbstractInput.h"

#include <utility>

namespace cla {

AbstractInput::AbstractInput() = default;

AbstractInput::~AbstractInput() = default;

void AbstractInput::connectToManager(StateManager& manager)
{
    m_managerInput = manager.addInput(shared_from_this());
}

/** @brief Interface for changing the state. Passes the state to manager input socket */
void AbstractInput::changeState(const State& state)
{
    if (auto ptr = m_managerInput.lock()) {
        ptr->setInputValue(state);
    }
}

}