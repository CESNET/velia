#pragma once

#include <boost/signals2/signal.hpp>
#include <functional>
#include <memory>
#include <vector>
#include "State.h"
#include "utils/log-fwd.h"

namespace velia {

class AbstractInput;

/**
 * @short Abstract interface for Manager.
 *
 * This class is responsible for collecting the input sources and notifying the registered outputs.
 */
class AbstractManager {
public:
    virtual ~AbstractManager();

    virtual void registerInput(void* input, State value) = 0;
    virtual void unregisterInput(void* input) = 0;

    virtual void updateState(void* input, State value) = 0;

    /** Output signal */
    boost::signals2::signal<void(State)> m_outputSignal;
};

}