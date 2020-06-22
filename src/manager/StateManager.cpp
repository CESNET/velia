#include <algorithm>
#include <utility>
#include "StateManager.h"
#include "utils/log.h"

namespace velia {

StateManager::StateManager()
    : m_log(spdlog::get("manager"))
{
}

StateManager::~StateManager() = default;

/** @brief Registers an input source */
void StateManager::registerInput(void* input, State value)
{
    m_log->trace("Registering input {}", input);

    if (m_inputs.find(input) != m_inputs.end()) {
        throw std::invalid_argument("Input already registered.");
    }

    m_inputs.insert(std::make_pair(input, value));
    computeOutput();
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

void StateManager::updateState(void* input, State value)
{
    if (auto it = m_inputs.find(input); it != m_inputs.end() && it->second != value) { // only on different state
        it->second = value;
        m_log->trace("Input {} changed state to {}", input, value);
        computeOutput();
    }
}

void StateManager::computeOutput()
{
    auto itMax = std::max_element(m_inputs.begin(), m_inputs.end(), [](const auto& e1, const auto& e2) { return e1.second < e2.second; });
    if (itMax != m_inputs.end()) { // fire a signal unless 0 inputs registered
        m_log->trace("Notifying registered outputs with state {}", itMax->second);
        m_outputSignal(itMax->second);
    }
}
}
