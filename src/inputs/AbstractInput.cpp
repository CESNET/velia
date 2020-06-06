#include "AbstractInput.h"

#include <utility>

namespace cla {

AbstractInput::AbstractInput(std::shared_ptr<MuxInputHandle> muxHandle)
    : m_muxHandle(std::move(muxHandle))
{
}

AbstractInput::~AbstractInput() = default;

void AbstractInput::changeState(State state)
{
    m_muxHandle->setInputValue(state);
}

}