#pragma once
#include <memory>
#include "health/State.h"
#include "health/outputs/SlotWrapper.h"

namespace velia::health {
boost::signals2::SlotWrapper<void, health::State> createOutput(const std::string& applianceName);
}
