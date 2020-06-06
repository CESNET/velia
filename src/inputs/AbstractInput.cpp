#include "AbstractInput.h"

#include <utility>

namespace velia {

AbstractInput::AbstractInput(std::shared_ptr<AbstractManager> manager)
    : m_manager(std::move(manager))
{
    m_manager->registerInput(this, State::OK);
}

AbstractInput::~AbstractInput()
{
    m_manager->unregisterInput(this);
}

/** @brief Interface for changing the state. Passes the state to manager input socket */
void AbstractInput::updateState(const State& state)
{
    m_manager->updateState(this, state);
}

}