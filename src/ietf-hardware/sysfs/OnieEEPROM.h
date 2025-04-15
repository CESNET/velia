/*
 * Copyright (C) 2025 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace velia::ietf_hardware::sysfs {

struct TLV {
    enum class Type : uint8_t {
        ProductName = 0x21,
        PartNumber = 0x22,
        SerialNumber = 0x23,
        MAC1Base = 0x24,
        ManufactureDate = 0x25,
        DeviceVersion = 0x26,
        LabelRevision = 0x27,
        PlatformName = 0x28,
        ONIEVersion = 0x29,
        NumberOfMAC = 0x2a,
        Manufacturer = 0x2b,
        CountryCode = 0x2c,
        Vendor = 0x2d,
        DiagnosticVersion = 0x2e,
        ServiceTag = 0x2f,
        VendorExtension = 0xfd,
    };

    Type type;
    using mac_addr_t = std::array<uint8_t, 6>;
    using Value = std::variant<std::string, uint8_t, uint16_t, std::vector<uint8_t>, mac_addr_t>;
    Value value;

    bool operator==(const TLV&) const = default;
};

using TlvInfo = std::vector<TLV>;

TlvInfo onieEeprom(const std::filesystem::path& eepromPath);
TlvInfo onieEeprom(const std::filesystem::path& sysfsPrefix, const uint8_t bus, const uint8_t address);

struct CzechLightData {
    std::string ftdiSN;
    std::vector<uint8_t> opticalData;
};

std::optional<CzechLightData> czechLightData(const TlvInfo& tlvs);
}
