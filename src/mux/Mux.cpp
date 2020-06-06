#include <algorithm>
#include "Mux.h"
#include "outputs/AbstractOutput.h"
#include "utils/log.h"

namespace cla {

Mux::Mux()
    : m_log(spdlog::get("mux"))
    , m_output(State::OK) // Everything is OK until set otherwise
{
}

/** @brief Create new input proxy. Callee is the owner. Returns only a reference to it. */
MuxInputHandle& Mux::createInput()
{
    auto handle = std::make_shared<MuxInputHandle>(*this);
    m_log->trace("Created new input (id={})", static_cast<void*>(handle.get()));

    m_inputs.push_back(handle);
    return *handle;
}

/** @brief Register output observer and immediately notify it */
void Mux::registerOutput(std::shared_ptr<AbstractOutput> out)
{
    m_outputs.push_back(out);
    out->update(getOutput());
}

State Mux::getOutput() const
{
    return m_output;
}

/** @brief Notify that one of the input values changed */
void Mux::notifyInputChanged(const MuxInputHandle& source)
{
    auto it = std::max_element(m_inputs.begin(), m_inputs.end(), [](const auto& e1, const auto& e2) { return e1->getInputValue() < e2->getInputValue(); });
    if ((*it)->getInputValue() != State::UNINITIALIZED) // If best is uninitialized, do nothing. // TODO: Maybe filter them before
        m_output = (*it)->getInputValue();

    m_log->trace("Input {} changed. Output is now {}.", static_cast<const void*>(&source), static_cast<std::underlying_type<State>::type>(m_output)); // TODO: op<< for State

    for (const auto& out : m_outputs) {
        out->update(getOutput());
    }
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

void MuxInputHandle::setInputValue(const State s)
{
    m_inputValue = s;
    m_mux.notifyInputChanged(*this);
}

}