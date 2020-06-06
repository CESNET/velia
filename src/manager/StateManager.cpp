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
std::shared_ptr<StateManagerInput> StateManager::addInput(std::shared_ptr<AbstractInput> input)
{
    auto handle = std::make_shared<StateManagerInput>(*this, input);
    m_log->trace("Input {} created", static_cast<void*>(handle.get()));

    m_inputs.push_back(handle);
    return handle;
}

const std::vector<std::shared_ptr<StateManagerInput>>& StateManager::getInputs() const
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
StateManagerInput::StateManagerInput(StateManager& manager, std::shared_ptr<AbstractInput> input)
    : m_manager(manager)
    , m_input(input)
    , m_inputValue(State::OK)
{
}

State StateManagerInput::getInputValue() const
{
    return m_inputValue;
}

void StateManagerInput::setInputValue(const State s)
{
    m_inputValue = s;
    m_manager.notifyInputChanged();
}
}