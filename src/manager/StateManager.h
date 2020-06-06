#pragma once

#include <boost/signals2/signal.hpp>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include "AbstractManager.h"
#include "State.h"
#include "utils/log-fwd.h"

namespace cla {

class AbstractInput;

/**
 * @short Stores registered inputs, output signal and also states of all currently registered inputs.
 */
class StateManager : public AbstractManager {
public:
    StateManager();
    ~StateManager() override;

    void registerInput(void* input, State value) override;
    void unregisterInput(void* input) override;

    void registerOutput(std::function<void(State)> callback) override;

    void updateState(void* input, State value) override;

private:
    cla::Log m_log;

    /** Registered inputs are identified by their memory location. The pointer only serves as an ID, this class does not manage the input pointers lifetime */
    std::map<void*, State> m_inputs;

    /** Output signal */
    boost::signals2::signal<void(State)> m_outputSignal;

    /** @brief Recompute output and fire output signal. Should be called on every input change. */
    void computeOutput();
};
}