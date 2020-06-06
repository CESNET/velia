#include "AbstractInput.h"

#include <utility>

namespace cla {

/** @brief Constructor. AbstractInput must be connected to cla::MuxInputHandle
 * @see cla::Mux::createInput
 */
AbstractInput::AbstractInput(StateManagerInputHandle& muxInput)
    : m_muxInput(muxInput)
{
    changeState(State::UNINITIALIZED);
}

AbstractInput::~AbstractInput()
{
    m_muxInput.deregister();
}

/** @brief Binds a value to this input */
void AbstractInput::changeState(State state)
{
    m_muxInput.setInputValue(state);
}

}