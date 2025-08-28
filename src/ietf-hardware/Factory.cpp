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
// ONIE says that it should be "MM/DD/YYYY HH:NN:SS"
const auto ONIE_MFG_DATE = std::regex{R"((\d{2})/(\d{2})/(\d{4}) (\d{2}):(\d{2}):(\d{2}))"};
// but on ClearFog, it is actually "2023-02-23 06:12:51" on our HW
const auto SOLIDRUN_ONIE_MFG_DATE = std::regex{R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})"};

std::string anyOnieDateToYangish(std::string date)
{
    std::smatch match;
    if (std::regex_match(date, SOLIDRUN_ONIE_MFG_DATE)) {
        date[10] = 'T';
        return date + "-00:00";
    }
    if (std::regex_match(date, match, ONIE_MFG_DATE)) {
        return fmt::format("{}-{}-{}T{}:{}:{}-00:00",
                           match[3].str(),
                           match[1].str(),
                           match[2].str(),
                           match[4].str(),
                           match[5].str(),
                           match[6].str());
    }
    throw std::runtime_error{"Cannot parse ONIE EEPROM date " + date};
}

std::string prettyName(const std::string& a, const std::string& b)
{
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    if (a == b) {
        return a;
    }
    return fmt::format("{} ({})", a, b);
}

void storeValues(velia::ietf_hardware::DataTree& target, const velia::ietf_hardware::sysfs::TlvInfo& tlvs)
{
    using velia::ietf_hardware::sysfs::TLV;
    std::string modelNamePN, modelNamePretty;
    std::string vendor, manufacturer;
    for (const auto& tlv : tlvs) {
        try {
            switch (tlv.type) {
            case TLV::Type::DeviceVersion:
                target["hardware-rev"] = fmt::format("{:d}", std::get<uint8_t>(tlv.value));
                break;
            case TLV::Type::ManufactureDate:
                target["mfg-date"] = anyOnieDateToYangish(std::get<std::string>(tlv.value));
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
                vendor = std::get<std::string>(tlv.value);
                break;
            case TLV::Type::Manufacturer:
                manufacturer = std::get<std::string>(tlv.value);
                break;
            case TLV::Type::MAC1Base:
            case TLV::Type::NumberOfMAC:
            case TLV::Type::LabelRevision:
            case TLV::Type::PlatformName:
            case TLV::Type::ONIEVersion:
            case TLV::Type::CountryCode:
            case TLV::Type::DiagnosticVersion:
            case TLV::Type::ServiceTag:
            case TLV::Type::VendorExtension:
                // unused, irrelevant, etc
                break;
            }
        } catch (const std::exception& e) {
            spdlog::get("hardware")->warn(
                    "Cannot store ONIE EEPROM TLV type {:#04x}: {}",
                    static_cast<int>(tlv.type),
                    e.what());
        }
    }
    target["model-name"] = prettyName(modelNamePretty, modelNamePN);
    target["mfg-name"] = prettyName(vendor, manufacturer);
}
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

std::shared_ptr<IETFHardware> createWithoutPower(const std::string& applianceName, const std::filesystem::path& sysfs)
{
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();

    if (applianceName == "czechlight-clearfog-g2") {
        auto fans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(sysfs / "bus/i2c/devices/1-0020/hwmon/");
        auto tempMainBoard = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(sysfs / "bus/i2c/devices/1-0048/hwmon/");
        auto tempFans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(sysfs / "bus/i2c/devices/1-0049/hwmon/");
        auto tempCpu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(sysfs / "devices/virtual/thermal/thermal_zone0/");
        auto tempMII0 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(sysfs / "devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:00/hwmon/");
        auto tempMII1 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(sysfs / "devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:01/hwmon/");
        auto emmc = std::make_shared<velia::ietf_hardware::sysfs::EMMC>(sysfs / "block/mmcblk0/device/");

        DataTree neData{
            {"class", "iana-hardware:chassis"},
        };
        DataTree ftdiData{
            {"class", "iana-hardware:port"},
            {"description", "USB serial console"},
        };
        try {
            const auto& tlvs = sysfs::onieEeprom(sysfs, 1, 0x53);
            storeValues(neData, tlvs);
            if (auto czechLightData = sysfs::czechLightData(tlvs)) {
                ftdiData["serial-num"] = czechLightData->ftdiSN;
            }
        } catch (const std::exception& e) {
            spdlog::get("hardware")->warn("Cannot parse SDN_IFACE EEPROM: {}", e.what());
        }
        ietfHardware->registerDataReader(StaticData("ne", std::nullopt, neData));
        ietfHardware->registerDataReader(StaticData("ne:ctrl:carrier:console", "ne:ctrl:carrier", ftdiData));

        ietfHardware->registerDataReader(StaticData{"ne:ctrl",
                                                    "ne",
                                                    {
                                                        {"class", "iana-hardware:module"},
                                                        {"serial-num", *hexEEPROM(sysfs, 1, 0x5b, 16, 0, 16)},
                                                    }});
        try {
            auto eeprom = hexEEPROM(sysfs, 1, 0x5a, 16, 0, 16);
            ietfHardware->registerDataReader(StaticData{"ne:voa-sw",
                                                        "ne",
                                                        {
                                                            {"class", "iana-hardware:module"},
                                                            {"serial-num", *eeprom},
                                                        }});
        } catch (const std::runtime_error& e) {
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
                                                        [sysfs]() {
                                                            return hexEEPROM(sysfs, 1, 0x5c, 16, 0, 16);
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
                const auto& tlvs = sysfs::onieEeprom(sysfs, 0, address);
                storeValues(target, tlvs);
                for (const auto& tlv : tlvs) {
                    try {
                        switch (tlv.type) {
                        case TLV::Type::DeviceVersion:
                            {
                                auto version = std::get<uint8_t>(tlv.value);
                                target["hardware-rev"] = fmt::format("{}.{}", version >> 4, version & 0x0f);
                            }
                            break;
                        default:
                            // ignore them, it's already handled elsewhere
                            break;
                        }
                    } catch (const std::exception& e) {
                        spdlog::get("hardware")->warn(
                                "Cannot store ONIE EEPROM TLV type {:#04x} from address {:#04x}: {}",
                                static_cast<int>(tlv.type),
                                static_cast<int>(address),
                                e.what());
                    }
                }
            } catch (const std::exception& e) {
                spdlog::get("hardware")->warn("Cannot parse ONIE EEPROM at {:#04x}: {}", static_cast<int>(address), e.what());
            }
        }
        ietfHardware->registerDataReader(StaticData{"ne:ctrl:som",
                                                    "ne:ctrl",
                                                    neCtrlSom});
        ietfHardware->registerDataReader(EepromWithUid{"ne:ctrl:som:eeprom", "ne:ctrl:som", sysfs, 0, 0x53, 256, 256 - 6, 6});
        ietfHardware->registerDataReader(StaticData{"ne:ctrl:carrier",
                                                    "ne:ctrl",
                                                    neCtrlCarrier});
        ietfHardware->registerDataReader(EepromWithUid{"ne:ctrl:carrier:eeprom", "ne:ctrl:carrier", sysfs, 0, 0x52, 256, 256 - 6, 6});
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-front", "ne:ctrl", tempMainBoard, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", tempCpu, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-rear", "ne:ctrl", tempFans, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-0", "ne:ctrl", tempMII0, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-1", "ne:ctrl", tempMII1, 1));
        ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));
    } else {
        throw std::runtime_error("Unknown appliance '" + applianceName + "'");
    }

    return ietfHardware;
}

std::shared_ptr<IETFHardware> create(const std::string& applianceName)
{
    auto ietfHardware = createWithoutPower(applianceName, "/sysfs");
    createPower(ietfHardware);
    return ietfHardware;
}

}
