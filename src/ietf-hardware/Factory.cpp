#include <future>
#include "Factory.h"
#include "FspYh.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"

namespace velia::ietf_hardware {
using velia::ietf_hardware::data_reader::EMMC;
using velia::ietf_hardware::data_reader::Fans;
using velia::ietf_hardware::data_reader::SensorType;
using velia::ietf_hardware::data_reader::StaticData;
using velia::ietf_hardware::data_reader::SysfsValue;

void createPower(std::shared_ptr<velia::ietf_hardware::IETFHardware> ietfHardware)
{
    auto pdu = std::make_shared<velia::ietf_hardware::FspYhPdu>("/sys/bus/i2c/devices/2-0025/hwmon",
                                                                "pdu",
                                                                std::make_shared<TransientI2C>(2, 0x25, "ym2151e"));
    auto psu1 = std::make_shared<velia::ietf_hardware::FspYhPsu>("/sys/bus/i2c/devices/2-0058/hwmon",
                                                                 "psu1",
                                                                 std::make_shared<TransientI2C>(2, 0x58, "ym2151e"));
    auto psu2 = std::make_shared<velia::ietf_hardware::FspYhPsu>("/sys/bus/i2c/devices/2-0059/hwmon",
                                                                 "psu2",
                                                                 std::make_shared<TransientI2C>(2, 0x59, "ym2151e"));

    struct ParallelPDUReader {
        std::shared_ptr<velia::ietf_hardware::FspYhPdu> pdu;
        std::shared_ptr<velia::ietf_hardware::FspYhPsu> psu1;
        std::shared_ptr<velia::ietf_hardware::FspYhPsu> psu2;

        ParallelPDUReader(std::shared_ptr<velia::ietf_hardware::FspYhPdu> pdu, std::shared_ptr<velia::ietf_hardware::FspYhPsu> psu1, std::shared_ptr<velia::ietf_hardware::FspYhPsu> psu2)
            : pdu(std::move(pdu))
            , psu1(std::move(psu1))
            , psu2(std::move(psu2))
        {
        }

        SensorPollData operator()()
        {
            auto psu1Reader = std::async(std::launch::async, [&] { return psu1->readValues(); });
            auto psu2Reader = std::async(std::launch::async, [&] { return psu2->readValues(); });
            auto pduReader = std::async(std::launch::async, [&] { return pdu->readValues(); });

            SensorPollData pollData;
            pollData.merge(psu1Reader.get());
            pollData.merge(psu2Reader.get());
            pollData.merge(pduReader.get());

            return pollData;
        }
    };

    ietfHardware->registerDataReader(ParallelPDUReader(pdu, psu1, psu2));
}

std::shared_ptr<IETFHardware> create(const std::string& applianceName)
{
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();

    if (applianceName == "czechlight-clearfog-g2") {
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
        ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}}));

        ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
        ietfHardware->registerDataReader(Fans("ne:fans",
                                              "ne",
                                              fans,
                                              4,
                                              Thresholds<int64_t>{
                                                  .criticalLow = OneThreshold<int64_t>{3680, 300}, /* 40 % of 9200 RPM */
                                                  .warningLow = OneThreshold<int64_t>{7360, 300}, /* 80 % of 9200 RPM */
                                                  .warningHigh = std::nullopt,
                                                  .criticalHigh = std::nullopt,
                                              }));
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
