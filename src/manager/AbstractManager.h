#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "State.h"
#include "utils/log-fwd.h"

namespace cla {

class AbstractInput;
class AbstractOutput;

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

    virtual void registerOutput(std::function<void(State)> callback) = 0;

    virtual void updateState(void* input, State value) = 0;
};

}