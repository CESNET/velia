#include <future>
#include <regex>
#include "Factory.h"
#include "FspYh.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"
#include "ietf-hardware/sysfs/OnieEEPROM.h"
#include "utils/log.h"

namespace {
// ONIE says that it should be "MM/DD/YYYY HH:NN:SS", it is actually "2023-02-23 06:12:51" on our HW
const auto SOLIDRUN_ONIE_MFG_DATE = std::regex{R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})"};
}

namespace velia::ietf_hardware {
using velia::ietf_hardware::data_reader::CzechLightFans;
using velia::ietf_hardware::data_reader::EMMC;
using velia::ietf_hardware::data_reader::EepromWithUid;
using velia::ietf_hardware::data_reader::SensorType;
using velia::ietf_hardware::data_reader::StaticData;
using velia::ietf_hardware::data_reader::SysfsValue;

void createPower(std::shared_ptr<velia::ietf_hardware::IETFHardware> ietfHardware)
{
    auto pdu = std::make_shared<velia::ietf_hardware::FspYhPdu>("pdu",
                                                                std::make_shared<TransientI2C>(2, 0x25, "yh5151"),
                                                                std::make_shared<TransientI2C>(2, 0x56, "24c02"));
    auto psu1 = std::make_shared<velia::ietf_hardware::FspYhPsu>("psu1",
                                                                 std::make_shared<TransientI2C>(2, 0x58, "ym2151"),
                                                                 std::make_shared<TransientI2C>(2, 0x50, "24c02"));
    auto psu2 = std::make_shared<velia::ietf_hardware::FspYhPsu>("psu2",
                                                                 std::make_shared<TransientI2C>(2, 0x59, "ym2151"),
                                                                 std::make_shared<TransientI2C>(2, 0x51, "24c02"));

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

        ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}}));

        ietfHardware->registerDataReader(StaticData{"ne:ctrl",
                                                    "ne",
                                                    {
                                                        {"class", "iana-hardware:module"},
                                                        {"serial-num", *hexEEPROM("/sys", 1, 0x5b, 16, 0, 16)},
                                                    }});
        try {
            auto eeprom = hexEEPROM("/sys", 1, 0x5a, 16, 0, 16);
            ietfHardware->registerDataReader(StaticData{"ne:voa-sw",
                                                        "ne",
                                                        {
                                                            {"class", "iana-hardware:module"},
                                                            {"serial-num", *eeprom},
                                                        }});
        } catch (std::runtime_error& e) {
            // this EEPROM is only present on regular inline amplifiers and on ROADM line/degree boxes
            // -> silently ignore any failures
        }

        ietfHardware->registerDataReader(CzechLightFans("ne:fans",
                                                        "ne",
                                                        fans,
                                                        4,
                                                        Thresholds<int64_t>{
                                                            .criticalLow = OneThreshold<int64_t>{3680, 300}, /* 40 % of 9200 RPM */
                                                            .warningLow = OneThreshold<int64_t>{7360, 300}, /* 80 % of 9200 RPM */
                                                            .warningHigh = std::nullopt,
                                                            .criticalHigh = std::nullopt,
                                                        },
                                                        []() {
                                                            return hexEEPROM("/sys", 1, 0x5c, 16, 0, 16);
                                                        }));
        DataTree neCtrlSom{
            {"class", "iana-hardware:module"},
            {"model-name", "ClearFog A388 SOM"},
        };
        DataTree neCtrlCarrier{
            {"class", "iana-hardware:module"},
            {"model-name", "ClearFog Base"},
        };
        // FIXME: C++23, zip()
        for (auto& [target, address] : std::to_array<std::pair<DataTree&, uint8_t>>({
                 {neCtrlSom, 0x53},
                 {neCtrlCarrier, 0x52},
             })) {
            using namespace velia::ietf_hardware::sysfs;
            try {
                std::string modelNamePN, modelNamePretty;
                for (const auto& tlv : sysfs::onieEeprom("/sys", 0, address)) {
                    try {
                        switch (tlv.type) {
                        case TLV::Type::DeviceVersion:
                            {
                                auto version = std::get<uint8_t>(tlv.value);
                                target["hardware-rev"] = fmt::format("{}.{}", version >> 4, version & 0x0f);
                            }
                            break;
                        case TLV::Type::ManufactureDate:
                            {
                                auto date = std::get<std::string>(tlv.value);
                                if (!std::regex_match(date, SOLIDRUN_ONIE_MFG_DATE)) {
                                    throw std::runtime_error{"Cannot parse ONIE EEPROM date " + date};
                                }
                                date[10] = 'T';
                                target["mfg-date"] = date + "-00:00";
                            }
                            break;
                        case TLV::Type::PartNumber:
                            modelNamePN = std::get<std::string>(tlv.value);
                            break;
                        case TLV::Type::ProductName:
                            modelNamePretty = std::get<std::string>(tlv.value);
                            break;
                        case TLV::Type::SerialNumber:
                            target["serial-num"] = std::get<std::string>(tlv.value);
                            break;
                        case TLV::Type::Vendor:
                            target["mfg-name"] = std::get<std::string>(tlv.value);
                            break;
                        case TLV::Type::VendorExtension:
                            // ignore this one; there doesn't appear to be anything relevant in that field on our boards
                            break;
                        }
                    } catch (std::exception& e) {
                        spdlog::get("hardware")->warn(
                                "Cannot store ONIE EEPROM TLV type {:#04x} from address {:#04x}: {}",
                                static_cast<int>(tlv.type),
                                static_cast<int>(address),
                                e.what());
                    }
                }
                if (!modelNamePN.empty() && !modelNamePretty.empty()) {
                    target["model-name"] = fmt::format("{} ({})", modelNamePretty, modelNamePN);
                } else if (!modelNamePN.empty()) {
                    target["model-name"] = modelNamePN;
                } else if (!modelNamePretty.empty()) {
                    target["model-name"] = modelNamePretty;
                }
            } catch (std::exception& e) {
                spdlog::get("hardware")->warn("Cannot parse ONIE EEPROM at {:#04x}: {}", static_cast<int>(address), e.what());
            }
        }
        ietfHardware->registerDataReader(StaticData{"ne:ctrl:som",
                                                    "ne:ctrl",
                                                    neCtrlSom});
        ietfHardware->registerDataReader(EepromWithUid{"ne:ctrl:som:eeprom", "ne:ctrl:som", "/sys", 0, 0x53, 256, 256 - 6, 6});
        ietfHardware->registerDataReader(StaticData{"ne:ctrl:carrier",
                                                    "ne:ctrl",
                                                    neCtrlCarrier});
        ietfHardware->registerDataReader(EepromWithUid{"ne:ctrl:carrier:eeprom", "ne:ctrl:carrier", "/sys", 0, 0x52, 256, 256 - 6, 6});
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
