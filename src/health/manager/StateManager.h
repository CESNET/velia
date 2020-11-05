#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include "AbstractManager.h"
#include "health/State.h"
#include "utils/log-fwd.h"

namespace velia::health {

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

    void updateState(void* input, State value) override;

private:
    velia::Log m_log;
    std::optional<State> m_oldState;

    /** Registered inputs are identified by their memory location. The pointer only serves as an ID, this class does not manage the input pointers lifetime */
    std::map<void*, State> m_inputs;

    /** @brief Recompute output and fire output signal. Should be called on every input change. */
    void computeOutput();
};
}
