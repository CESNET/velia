#pragma once

#include "State.h"

namespace cla {

/**
 * @short Abstract interface for classes outputting AbstractManager's state.
 */
class AbstractOutput {
public:
    virtual void update(State state) = 0;
};

}