#include "AbstractInput.h"

#include <utility>

namespace cla {

/** @brief Constructor. AbstractInput must be connected to cla::MuxInputHandle
 * @see cla::Mux::createInput
 */
AbstractInput::AbstractInput() = default;

AbstractInput::~AbstractInput() = default;

void AbstractInput::connectToManager(StateManager& manager)
{
    m_muxInput = manager.addInput(shared_from_this());
}

/** @brief Binds a value to this input */
void AbstractInput::changeState(State state)
{
    if (auto ptr = m_muxInput.lock()) {
        ptr->setInputValue(state);
    }
}

}