#pragma once
#include <memory>
#include "health/State.h"
#include "health/outputs/callables.h"

namespace velia::health {
LedOutputCallback createOutput(const std::string& applianceName);
}
