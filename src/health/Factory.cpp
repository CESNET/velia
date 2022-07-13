#include "Factory.h"
#include "health/outputs/LedSysfsDriver.h"

namespace velia::health {
LedOutputCallback createOutput(const std::string& applianceName)
{
    if (applianceName == "czechlight-clearfog") {
        return LedOutputCallback(
            std::make_shared<LedSysfsDriver>("/sys/class/leds/status:red/"),
            std::make_shared<LedSysfsDriver>("/sys/class/leds/status:green/"),
            std::make_shared<LedSysfsDriver>("/sys/class/leds/status:blue/"));
    } else {
        throw std::runtime_error("Unknown appliance '" + applianceName + "'");
    }
}
}
