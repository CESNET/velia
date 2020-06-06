#pragma once

#include <memory>
#include <vector>
#include "State.h"
#include "utils/log-fwd.h"

namespace cla {

class StateManagerInputHandle;

/**
 * @short Class responsible for computing the state from multiple inputs.
 *
 * This class is responsible for collecting the current inputs. It does so via StateManagerInputHandle class.
 */
class StateManager {
public:
    StateManager();

    StateManagerInputHandle& createInput();
    void removeInput(StateManagerInputHandle& input);
    const std::vector<std::shared_ptr<StateManagerInputHandle>>& getInputs() const;

    void notifyInputChanged();
    State getOutput() const;

private:
    cla::Log m_log;

    /** List of registered inputs */
    std::vector<std::shared_ptr<StateManagerInputHandle>> m_inputs;

    /** Currently outputted value */
    State m_output;
};

/**
 * @short Represents an input of the Mux class. Inputs should set the state through this proxy class.
 */
class StateManagerInputHandle {
public:
    explicit StateManagerInputHandle(StateManager& mux);
    void setInputValue(State s);
    State getInputValue() const;
    void deregister();

private:
    StateManager& m_mux;
    State m_inputValue;
};
}