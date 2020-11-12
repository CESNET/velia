#include "Factory.h"
#include "health/outputs/LedSysfsDriver.h"
#include "health/outputs/SlotWrapper.h"
#include "health/outputs/callables.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"

namespace velia::ietf_hardware {

std::shared_ptr<IETFHardware> create(const std::string& applianceName)
{
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();

    if (applianceName == "czechlight-clearfog") {
        auto hwmonFans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-002e/hwmon/");
        auto sysfsTempFront = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1011100.i2c/i2c-1/1-002e/hwmon/");
        auto sysfsTempCpu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/virtual/thermal/thermal_zone0/");
        auto sysfsTempMII0 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:00/hwmon/");
        auto sysfsTempMII1 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:01/hwmon/");
        auto emmc = std::make_shared<velia::ietf_hardware::sysfs::EMMC>("/sys/block/mmcblk0/device/");

        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}})); // FIXME: We have an EEPROM at the PCB for storing these information, but it's so far unused. We could also use U-Boot env variables for this.
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::Fans("ne:fans", "ne", hwmonFans, 4));
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));
    } else {
        throw std::runtime_error("Unknown appliance '" + applianceName + "'");
    }

    return ietfHardware;
}

}

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
