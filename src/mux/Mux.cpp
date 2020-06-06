#include <algorithm>
#include "Mux.h"
#include "utils/log.h"

namespace cla {

Mux::Mux()
    : m_log(spdlog::get("mux"))
    , m_output(State::OK)
{
}

/** @brief Create new input proxy */
std::shared_ptr<MuxInputHandle> Mux::createInput()
{
    m_inputs.push_back(std::make_shared<MuxInputHandle>(*this));
    m_log->trace("Created new input (id={})", (void*)m_inputs.back().get());
    return m_inputs.back();
}

State Mux::getOutput() const
{
    return m_output;
}

/** @brief Notify that one of the input values changed */
void Mux::notifyInputChanged(const MuxInputHandle& source)
{
    m_log->trace("Input {} changed", (void*)&source);

    auto it = std::max_element(m_inputs.begin(), m_inputs.end(), [](const auto& e1, const auto& e2) { return e1->getInputValue() < e2->getInputValue(); });
    m_output = (*it)->getInputValue();
}

MuxInputHandle::MuxInputHandle(Mux& mux)
    : m_mux(mux)
    , m_inputValue(State::OK)
{
}

State MuxInputHandle::getInputValue() const
{
    return m_inputValue;
}

void MuxInputHandle::setInputValue(State s)
{
    m_inputValue = s;
    m_mux.notifyInputChanged(*this);
}

}