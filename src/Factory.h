#pragma once
#include <memory>
#include "health/State.h"
#include "health/outputs/SlotWrapper.h"

namespace velia::ietf_hardware {
class IETFHardware;
}

namespace velia::ietf_hardware {
std::shared_ptr<ietf_hardware::IETFHardware> createIETFHardware(const std::string& applianceName);
}

namespace velia::health {
boost::signals2::SlotWrapper<void, health::State> createOutput(const std::string& applianceName);
}
