#include "Factory.h"
#include "health/outputs/LedSysfsDriver.h"
#include "health/outputs/SlotWrapper.h"
#include "health/outputs/callables.h"

namespace velia::health {
boost::signals2::SlotWrapper<void, health::State> createOutput(const std::string& applianceName)
{
    if (applianceName == "czechlight-clearfog") {
        return boost::signals2::SlotWrapper<void, State>(std::make_shared<LedOutputCallback>(
            std::make_shared<LedSysfsDriver>("/sys/class/leds/status:red/"),
            std::make_shared<LedSysfsDriver>("/sys/class/leds/status:green/"),
            std::make_shared<LedSysfsDriver>("/sys/class/leds/status:blue/")));
    } else {
        throw std::runtime_error("Unknown appliance '" + applianceName + "'");
    }
}
}
