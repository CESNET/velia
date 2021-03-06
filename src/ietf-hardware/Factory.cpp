#include "Factory.h"
#include "FspYhPsu.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"

namespace velia::ietf_hardware {
using velia::ietf_hardware::data_reader::StaticData;
using velia::ietf_hardware::data_reader::Fans;
using velia::ietf_hardware::data_reader::SysfsValue;
using velia::ietf_hardware::data_reader::EMMC;
using velia::ietf_hardware::data_reader::SensorType;

void createPower(std::shared_ptr<velia::ietf_hardware::IETFHardware> ietfHardware)
{
    auto pdu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/2-0025/hwmon");
    ietfHardware->registerDataReader(StaticData("ne:pdu", "ne", {{"class", "iana-hardware:power-supply"}}));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageDC>("ne:pdu:voltage-12V", "ne:pdu", pdu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageDC>("ne:pdu:voltage-5V", "ne:pdu", pdu, 2));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageDC>("ne:pdu:voltage-3V3", "ne:pdu", pdu, 3));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Current>("ne:pdu:current-12V", "ne:pdu", pdu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Current>("ne:pdu:current-5V", "ne:pdu", pdu, 2));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Current>("ne:pdu:current-3V3", "ne:pdu", pdu, 3));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:pdu:temperature-1", "ne:pdu", pdu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:pdu:temperature-2", "ne:pdu", pdu, 2));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:pdu:temperature-3", "ne:pdu", pdu, 3));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:pdu:power-12V", "ne:pdu", pdu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:pdu:power-5V", "ne:pdu", pdu, 2));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:pdu:power-3V3", "ne:pdu", pdu, 3));

    ietfHardware->registerDataReader([psu =
            std::make_shared<velia::ietf_hardware::FspYhPsu>("/sys/bus/i2c/devices/2-0058/hwmon",
                                                             "psu1",
                                                             std::make_shared<TransientI2C>(2, 0x58, "ym2151e"))] {
        return psu->readValues();
    });

    ietfHardware->registerDataReader([psu =
            std::make_shared<velia::ietf_hardware::FspYhPsu>("/sys/bus/i2c/devices/2-0059/hwmon",
                                                             "psu2",
                                                             std::make_shared<TransientI2C>(2, 0x59, "ym2151e"))] {
        return psu->readValues();
    });
}

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

        ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"description", "Czechlight project"s}}));
        ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
        ietfHardware->registerDataReader(Fans("ne:fans", "ne", hwmonFans, 4));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
        ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

        createPower(ietfHardware);
    } else if (applianceName == "czechlight-clearfog-g2") {
        auto fans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-0020/hwmon/");
        auto tempMainBoard = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-0048/hwmon/");
        auto tempFans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-0049/hwmon/");
        auto tempCpu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/virtual/thermal/thermal_zone0/");
        auto tempMII0 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:00/hwmon/");
        auto tempMII1 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:01/hwmon/");
        auto emmc = std::make_shared<velia::ietf_hardware::sysfs::EMMC>("/sys/block/mmcblk0/device/");

        /* FIXME:
         * Publish more properties for ne element. We have an EEPROM at the PCB for storing serial numbers (etc.), but it's so far unused. We could also use U-Boot env variables
         * This will be needed for sdn-roadm-line only. So we should also parse the model from /proc/cmdline here
         */
        ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"description", "Czechlight project"s}}));

        ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
        ietfHardware->registerDataReader(Fans("ne:fans", "ne", fans, 4));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-front", "ne:ctrl", tempMainBoard, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", tempCpu, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-rear", "ne:ctrl", tempFans, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-0", "ne:ctrl", tempMII0, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-1", "ne:ctrl", tempMII1, 1));
        ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

        createPower(ietfHardware);
    } else {
        throw std::runtime_error("Unknown appliance '" + applianceName + "'");
    }

    return ietfHardware;
}

}
