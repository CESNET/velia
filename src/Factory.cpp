#include "Factory.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"

namespace velia::factory {

std::shared_ptr<ietf_hardware::IETFHardware> initializeCzechlightClearfogIETFHardware()
{
    auto hwState = std::make_shared<velia::ietf_hardware::IETFHardware>();
    auto hwmonFans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-002e/hwmon/");
    auto sysfsTempFront = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1011100.i2c/i2c-1/1-002e/hwmon/");
    auto sysfsTempCpu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/virtual/thermal/thermal_zone0/");
    auto sysfsTempMII0 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:00/hwmon/");
    auto sysfsTempMII1 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:01/hwmon/");
    auto emmc = std::make_shared<velia::ietf_hardware::sysfs::EMMC>("/sys/block/mmcblk0/device/");

    hwState->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}})); // FIXME: We have an EEPROM at the PCB for storing these information, but it's so far unused. We could also use U-Boot env variables for this.
    hwState->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::Fans("ne:fans", "ne", hwmonFans, 4));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

    return hwState;
}
}
