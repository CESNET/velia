/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace velia::ietf_hardware::sysfs {

struct CommonHeader {
    uint8_t internalUseAreaOfs;
    uint8_t chassisInfoAreaOfs;
    uint8_t boardAreaOfs;
    uint8_t productInfoAreaOfs;
    uint8_t multiRecordAreaOfs;

    bool operator==(const CommonHeader&) const = default;
};

struct ProductInfo {
    std::string manufacturer;
    std::string name;
    std::string partNumber;
    std::string version;
    std::string serialNumber;
    std::string assetTag;
    std::string fruFileId;
    std::vector<std::string> custom;

    bool operator==(const ProductInfo&) const = default;
};

struct FRUInformationStorage {
    CommonHeader header;
    ProductInfo productInfo;

    bool operator==(const FRUInformationStorage&) const = default;
};

FRUInformationStorage ipmiFruEeprom(const std::filesystem::path& eepromPath);
FRUInformationStorage ipmiFruEeprom(const std::filesystem::path& sysfsPrefix, const uint8_t bus, const uint8_t address);
}
