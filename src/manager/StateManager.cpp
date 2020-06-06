#include <algorithm>
#include <utility>
#include "StateManager.h"
#include "outputs/AbstractOutput.h"
#include "utils/log.h"

namespace cla {

StateManager::StateManager()
    : m_log(spdlog::get("manager"))
{
}

StateManager::~StateManager() = default;


void StateManager::registerInput(void* input, State value)
{
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_inputs.count(input)) {
            throw std::invalid_argument("Input already registered.");
        }
    }

    m_log->trace("Registering input {}", input);
    updateState(input, value);
}

void StateManager::unregisterInput(void* input)
{
    {
        std::lock_guard<std::mutex> lock(m_mtx);

        auto it = m_inputs.find(input);
        if (it == m_inputs.end()) {
            throw std::invalid_argument("Input not registered.");
        }

        m_log->trace("Unregistering input {}", input);
        m_inputs.erase(it);
    }

    computeOutput();
}

void StateManager::registerOutput(std::shared_ptr<AbstractOutput> output)
{
    if (std::find(m_outputs.begin(), m_outputs.end(), output) != m_outputs.end()) {
        throw std::invalid_argument("Output already registered.");
    }
    m_log->trace("Registering output {}", (void*)output.get());

    m_outputs.push_back(std::move(output));
    computeOutput();
}

void StateManager::unregisterOutput(std::shared_ptr<AbstractOutput> output)
{
    auto it = std::find(m_outputs.begin(), m_outputs.end(), output);
    if (it == m_outputs.end()) {
        throw std::invalid_argument("Output not registered.");
    }

    m_log->trace("Unregistering output {}", (void*)output.get());
    m_outputs.erase(it);
}

void StateManager::updateState(void* input, State value)
{
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_inputs[input] = value;
    }
    computeOutput();
}

void StateManager::computeOutput()
{
    decltype(m_inputs)::const_iterator itMax;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        itMax = std::max_element(m_inputs.begin(), m_inputs.end(), [](const auto& e1, const auto& e2) { return e1.second < e2.second; });
    }

    if (itMax != m_inputs.end()) { // 0 inputs
        notifyOutputs(itMax->second);
    }
}

void StateManager::notifyOutputs(State state)
{
    m_log->trace("Notifying registered outputs with state {}", static_cast<std::underlying_type<State>::type>(state)); // TODO: op<< for State?
    for (auto& output : m_outputs) {
        output->update(state);
    }
}
}
