#pragma once
#include <memory>

namespace velia::ietf_hardware {
class IETFHardware;
}

namespace velia::ietf_hardware {
std::shared_ptr<ietf_hardware::IETFHardware> create(const std::string& applianceName);
}
