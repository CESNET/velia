#pragma once

#include <memory>
#include <vector>
#include "State.h"
#include "utils/log-fwd.h"

namespace cla {

class AbstractInput;
class StateManagerInput;

/**
 * @short Class responsible for computing the state from multiple inputs.
 *
 * This class is responsible for collecting the current inputs. It does so via StateManagerInputHandle class.
 */
class StateManager {
public:
    StateManager();

    std::shared_ptr<StateManagerInput> addInput(std::shared_ptr<AbstractInput> input);
    const std::vector<std::shared_ptr<StateManagerInput>>& getInputs() const;

    void notifyInputChanged();
    State getOutput() const;

private:
    cla::Log m_log;

    /** List of registered inputs */
    std::vector<std::shared_ptr<StateManagerInput>> m_inputs;

    /** Currently outputted value */
    State m_output;
};

/**
 * @short Represents an input of the Mux class. Inputs should set the state through this proxy class.
 */
class StateManagerInput {
public:
    explicit StateManagerInput(StateManager& mux, std::shared_ptr<AbstractInput> input);
    void setInputValue(State s);
    State getInputValue() const;

private:
    StateManager& m_manager;
    std::shared_ptr<AbstractInput> m_input;
    State m_inputValue;
};
}