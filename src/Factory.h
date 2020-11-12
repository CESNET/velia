#pragma once
#include <memory>

namespace velia::ietf_hardware {
class IETFHardware;
}

namespace velia::factory {
std::shared_ptr<ietf_hardware::IETFHardware> initializeCzechlightClearfogIETFHardware();
}
