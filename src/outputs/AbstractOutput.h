#pragma once

#include "State.h"

namespace cla {

/** @brief Basically Mux's observer */
class AbstractOutput {
public:
    virtual void update(const State state) = 0;
};

}