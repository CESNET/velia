#include <algorithm>
#include <utility>
#include "StateManager.h"
#include "utils/log.h"

namespace cla {

StateManager::StateManager()
    : m_log(spdlog::get("manager"))
{
}

StateManager::~StateManager() = default;

/** @brief Registers an input source */
void StateManager::registerInput(void* input, State value)
{
    m_log->trace("Registering input {}", input);

    if (m_inputs.count(input)) {
        throw std::invalid_argument("Input already registered.");
    }

    updateState(input, value);
}

/** @brief Unregisters an input source */
void StateManager::unregisterInput(void* input)
{
    m_log->trace("Unregistering input {}", input);

    auto it = m_inputs.find(input);
    if (it == m_inputs.end()) {
        throw std::invalid_argument("Input not registered.");
    }

    m_inputs.erase(it);
    computeOutput();
}

void StateManager::registerOutput(std::function<void(State)> callback)
{
    m_outputSignal.connect(callback);
}

void StateManager::updateState(void* input, State value)
{
    m_inputs[input] = value;
    computeOutput();
}

void StateManager::computeOutput()
{
    auto itMax = std::max_element(m_inputs.begin(), m_inputs.end(), [](const auto& e1, const auto& e2) { return e1.second < e2.second; });
    if (itMax != m_inputs.end()) { // fire a signal unless 0 inputs registered
        m_log->trace("Notifying registered outputs with state {}", static_cast<std::underlying_type<State>::type>(itMax->second)); // TODO: op<< for State?
        m_outputSignal(itMax->second);
    }
}
}
