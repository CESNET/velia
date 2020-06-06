#include <algorithm>
#include "StateManager.h"
#include "utils/log.h"

namespace cla {

StateManager::StateManager()
    : m_log(spdlog::get("mux"))
    , m_output(State::OK) // Everything is OK until set otherwise
{
}

/** @brief Create new input proxy. Callee is the owner. Returns only a reference to it. */
StateManagerInputHandle& StateManager::createInput()
{
    auto handle = std::make_shared<StateManagerInputHandle>(*this);
    m_log->trace("Input {} created", static_cast<void*>(handle.get()));

    m_inputs.push_back(handle);
    return *handle;
}

/** @brief Removes an input. */
void StateManager::removeInput(StateManagerInputHandle& input)
{
    auto it = std::find_if(m_inputs.begin(), m_inputs.end(), [&input](const std::shared_ptr<StateManagerInputHandle>& i) { return i.get() == &input; });
    if (it != m_inputs.end()) {
        m_inputs.erase(it);
        m_log->trace("Input {} removed", static_cast<const void*>(&input));
        notifyInputChanged();
    } else {
        throw std::invalid_argument("Removing unregistered input.");
    }
}

const std::vector<std::shared_ptr<StateManagerInputHandle>>& StateManager::getInputs() const
{
    return m_inputs;
}

State StateManager::getOutput() const
{
    return m_output;
}

/** @brief Notify that one of the input values changed */
void StateManager::notifyInputChanged()
{
    auto it = std::max_element(m_inputs.begin(), m_inputs.end(), [](const auto& e1, const auto& e2) { return e1->getInputValue() < e2->getInputValue(); });
    if (it != m_inputs.end() && (*it)->getInputValue() != State::UNINITIALIZED) // If best is uninitialized, do nothing. // TODO: Maybe filter them before
        m_output = (*it)->getInputValue();
    else
        m_output = State::OK;

    m_log->trace("Output is now {}.", static_cast<std::underlying_type<State>::type>(m_output)); // TODO: op<< for State
}

/** @brief Creates an input proxy class for specific mux */
StateManagerInputHandle::StateManagerInputHandle(StateManager& mux)
    : m_mux(mux)
    , m_inputValue(State::OK)
{
}

State StateManagerInputHandle::getInputValue() const
{
    return m_inputValue;
}

void StateManagerInputHandle::setInputValue(const State s)
{
    m_inputValue = s;
    m_mux.notifyInputChanged();
}
void StateManagerInputHandle::deregister()
{
    m_mux.removeInput(*this);
}
}