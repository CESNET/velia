#pragma once

#include <map>
#include <memory>
#include <mutex>
#include "AbstractManager.h"
#include "State.h"
#include "utils/log-fwd.h"

namespace cla {

class AbstractInput;
class AbstractOutput;

/**
 * @short Stores registered inputs, outputs and also states of registered inputs. It computes an output value from such states.
 */
class StateManager : public AbstractManager {
public:
    StateManager();
    ~StateManager() override;

    void registerInput(void* input, State value) override;
    void unregisterInput(void* input) override;

    void registerOutput(std::shared_ptr<AbstractOutput> output) override;
    void unregisterOutput(std::shared_ptr<AbstractOutput> output) override;

    void updateState(void* input, State value) override;

private:
    cla::Log m_log;

    std::mutex m_mtx;

    /** Registered inputs */
    std::map<void*, State> m_inputs;

    /** Registered outputs */
    std::vector<std::shared_ptr<AbstractOutput>> m_outputs;

    /** @brief Compute output. Should be called whenever any input changes. */
    void computeOutput();

    /** @brief Notify all outputs about change in the Manager's output */
    void notifyOutputs(State state);
};
}