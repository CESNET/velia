#pragma once

#include <memory>
#include <vector>
#include "State.h"
#include "utils/log-fwd.h"

namespace cla {

class MuxInputHandle;
class AbstractOutput;

/**
 * @short Class responsible for computing the state from multiple inputs.
 */
class Mux {
public:
    Mux();

    MuxInputHandle& createInput();
    void registerOutput(std::shared_ptr<AbstractOutput> out);

    void notifyInputChanged(const MuxInputHandle& source);
    State getOutput() const;

private:
    cla::Log m_log;

    /** List of registered inputs */
    std::vector<std::shared_ptr<MuxInputHandle>> m_inputs;
    std::vector<std::shared_ptr<AbstractOutput>> m_outputs;

    /** Current outputted value */
    State m_output;
};

/**
 * @short Represents an input of the Mux class. Inputs should set the state through this proxy class.
 */
class MuxInputHandle {
public:
    explicit MuxInputHandle(Mux& mux);
    void setInputValue(State s);
    State getInputValue() const;

private:
    Mux& m_mux;
    State m_inputValue;
};

}