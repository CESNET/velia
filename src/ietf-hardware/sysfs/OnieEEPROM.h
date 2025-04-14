/*
 * Copyright (C) 2025 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace velia::ietf_hardware::sysfs {

struct TLV {
    enum class Type {
        ProductName = 0x21,
        PartNumber = 0x22,
        SerialNumber = 0x23,
        MAC1Base = 0x24,
        ManufactureDate = 0x25,
        DeviceVersion = 0x26,
        Vendor = 0x2d,
        VendorExtension = 0xfd,
    };

    Type type;
    using Value = std::variant<std::string, uint8_t, std::vector<uint8_t>>;
    Value value;

    bool operator==(const TLV&) const = default;
};

using TlvInfo = std::vector<TLV>;

TlvInfo onieEeprom(const std::filesystem::path& eepromPath);
TlvInfo onieEeprom(const std::filesystem::path& sysfsPrefix, const uint8_t bus, const uint8_t address);
}
