/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysfs/IpmiFruEEPROM.h"
#include "ietf-hardware/sysfs/OnieEEPROM.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;
const auto sysfsPrefix = CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/"s;
using namespace velia::ietf_hardware;

TEST_CASE("EEPROM with UID/EID")
{
    using namespace data_reader;
    TEST_INIT_LOGS;

    REQUIRE(*hexEEPROM(sysfsPrefix, 1, 0x5c, 16, 0, 16) == "1E70C61C941000628C2EA000A000000C");
    REQUIRE(*hexEEPROM(sysfsPrefix, 1, 0x5c, 16, 0, 15) == "1E70C61C941000628C2EA000A00000");

    auto working = EepromWithUid("x:eeprom", "x", sysfsPrefix, 0, 0x52, 256, 256 - 6, 6);
    REQUIRE(working().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/serial-num", "294100B13DA3"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "enabled"},
            });

    auto missing = EepromWithUid("x:eeprom", "x", sysfsPrefix, 0, 0x53, 256, 256 - 6, 6);
    REQUIRE(missing().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "disabled"},
            });

    auto corrupted = EepromWithUid("x:eeprom", "x", sysfsPrefix, 0, 0x53, 16, 2, 6);
    REQUIRE(corrupted().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "disabled"},
            });

    REQUIRE_THROWS_WITH(EepromWithUid("x:eeprom", "x", sysfsPrefix, 0, 0x20, 256, 256 - 6, 6),
                        "EEPROM: no I2C device defined at bus 0 address 0x20");

    REQUIRE_THROWS_WITH(hexEEPROM(sysfsPrefix, 0, 0, 10, 5, 6),
                        "EEPROM: region out of range");
}

TEST_CASE("IPMI FRU EEPROM reader")
{
    using namespace sysfs;
    TEST_INIT_LOGS;

    const auto testsDir = std::filesystem::path{sysfsPrefix} / "eeprom"s;

    std::string eepromFile;

    DOCTEST_SUBCASE("Valid files")
    {
        FRUInformationStorage expected;
        DOCTEST_SUBCASE("SDN-ID210512_eeprom-2-0056.bin")
        {
            eepromFile = "SDN-ID210512_eeprom-2-0056.bin";
            expected = FRUInformationStorage{
                .header = CommonHeader(0, 0, 0, 1, 11),
                .productInfo = ProductInfo{
                    .manufacturer = "3Y POWER",
                    .name = "URP1X151AH",
                    .partNumber = "YH-5151E",
                    .version = "B01R",
                    .serialNumber = "SA140T302044001013",
                    .assetTag = "",
                    .fruFileId = "P2J700A01",
                    .custom = {"A14"},
                }};
        }
        DOCTEST_SUBCASE("M0N_eeprom-2-0050.bin")
        {
            eepromFile = "M0N_eeprom-2-0050.bin";
            expected = FRUInformationStorage{
                .header = CommonHeader(0, 0, 0, 1, 11),
                .productInfo = ProductInfo{
                    .manufacturer = "3Y POWER",
                    .name = "URP1X151AM",
                    .partNumber = "YM-2151E",
                    .version = "B01R",
                    .serialNumber = "SA010T291647000517",
                    .assetTag = "",
                    .fruFileId = "P2J700A00",
                    .custom = {"A01"},
                }};
        }

        DOCTEST_SUBCASE("M0N_eeprom-2-0051.bin")
        {
            eepromFile = "M0N_eeprom-2-0051.bin";
            expected = FRUInformationStorage{
                .header = CommonHeader(0, 0, 0, 1, 11),
                .productInfo = ProductInfo{
                    .manufacturer = "3Y POWER",
                    .name = "URP1X151AM",
                    .partNumber = "YM-2151E",
                    .version = "B01R",
                    .serialNumber = "SA010T291647000518",
                    .assetTag = "",
                    .fruFileId = "P2J700A00",
                    .custom = {"A01"},
                }};
        }
        DOCTEST_SUBCASE("M0N_eeprom-2-0056.bin")
        {
            eepromFile = "M0N_eeprom-2-0056.bin";
            expected = FRUInformationStorage{
                .header = CommonHeader(0, 0, 0, 1, 11),
                .productInfo = ProductInfo{
                    .manufacturer = "3Y POWER",
                    .name = "URP1X151AH",
                    .partNumber = "YH-5151E",
                    .version = "B01R",
                    .serialNumber = "SA020T301647000259",
                    .assetTag = "",
                    .fruFileId = "P2J700A00",
                    .custom = {"A02"},
                }};
        }
        DOCTEST_SUBCASE("YM-2151F.bin")
        {
            eepromFile = "YM-2151F.bin";
            expected = FRUInformationStorage{
                .header = CommonHeader{0, 0, 0, 1, 11},
                .productInfo = ProductInfo{
                    .manufacturer = "3Y POWER",
                    .name = "URP1X151DM",
                    .partNumber = "YM-2151F",
                    .version = "BR  ",
                    .serialNumber = "SB090S512343000017",
                    .assetTag = "",
                    .fruFileId = "P20000A00",
                    .custom = {"B09"},
                },
            };
        }
        DOCTEST_SUBCASE("Wrong product area length")
        {
            DOCTEST_SUBCASE("1")
            {
                eepromFile = "SDN-ID210512_eeprom-2-0050_wrong_prodarea_len.bin";
                expected = FRUInformationStorage{
                    .header = CommonHeader(0, 0, 0, 1, 12),
                    .productInfo = ProductInfo{
                        .manufacturer = "3Y POWER",
                        .name = "URP1X151AM",
                        .partNumber = "YM-2151E",
                        .version = "B01R       ",
                        .serialNumber = "SA110T292044002126 ",
                        .assetTag = "",
                        .fruFileId = "P2J700A04",
                        .custom = {"A11"},
                    }};
            }
            DOCTEST_SUBCASE("2")
            {
                eepromFile = "SDN-ID210512_eeprom-2-0051_wrong_prodarea_len.bin";
                expected = FRUInformationStorage{
                    .header = CommonHeader(0, 0, 0, 1, 12),
                    .productInfo = ProductInfo{
                        .manufacturer = "3Y POWER",
                        .name = "URP1X151AM",
                        .partNumber = "YM-2151E",
                        .version = "B01R       ",
                        .serialNumber = "SA110T292044002125 ",
                        .assetTag = "",
                        .fruFileId = "P2J700A04",
                        .custom = {"A11"},
                    }};
            }
        }

        REQUIRE(ipmiFruEeprom(testsDir / eepromFile) == expected);
    }

    DOCTEST_SUBCASE("Invalid files")
    {
        std::string exception;

        DOCTEST_SUBCASE("Wrong product area length")
        {
            // our hack doesn't apply because the size is given as an "unexpected" number
            eepromFile = "very_wrong_prodarea_len.bin";
            exception = "padding overflow: ate 83 bytes, total expected size = 72";
        }
        DOCTEST_SUBCASE("Wrong header checksum")
        {
            eepromFile = "wrong_header_checksum.bin";
            exception = "checksum error: bytes sum to 0x01";
        }
        DOCTEST_SUBCASE("format is not 0x01, correct checksum")
        {
            eepromFile = "wrong_header_format.bin";
            exception = "failed to parse Common Header";
        }
        DOCTEST_SUBCASE("pad is not 0x00, correct checksum")
        {
            eepromFile = "wrong_header_pad.bin";
            exception = "failed to parse Common Header";
        }

        REQUIRE_THROWS_WITH_AS(ipmiFruEeprom(testsDir / eepromFile), exception.c_str(), std::runtime_error);
    }
}

TEST_CASE("ONIE EEPROM reader")
{
    using velia::ietf_hardware::sysfs::TLV;
    using velia::ietf_hardware::sysfs::TlvInfo;
    TEST_INIT_LOGS;

    const auto testsDir = std::filesystem::path{sysfsPrefix} / "eeprom"s;
    std::filesystem::path eepromFile;

    DOCTEST_SUBCASE("Valid files")
    {
        TlvInfo expected;

        DOCTEST_SUBCASE("188_0-0052_eeprom.bin")
        {
            eepromFile = "188_0-0052_eeprom.bin";
            expected = TlvInfo({
                TLV{.type = TLV::Type::ProductName, .value = "Clearfog Base"},
                TLV{.type = TLV::Type::PartNumber, .value = "SRCFCBE000CV14"},
                TLV{.type = TLV::Type::SerialNumber, .value = "IP01195230800010"},
                TLV{.type = TLV::Type::ManufactureDate, .value = "2023-02-23 06:12:51"},
                TLV{.type = TLV::Type::DeviceVersion, .value = uint8_t{0x14}},
                TLV{.type = TLV::Type::Vendor, .value = "SolidRun"},
                TLV{.type = TLV::Type::VendorExtension, .value = std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0x81, 0x04}},
            });
        }
        DOCTEST_SUBCASE("188_0-0053_eeprom.bin")
        {
            eepromFile = "188_0-0053_eeprom.bin";
            expected = TlvInfo({
                TLV{.type = TLV::Type::ProductName, .value = "A38x SOM"},
                TLV{.type = TLV::Type::PartNumber, .value = "SRM6828S32D01GE008V21C0"},
                TLV{.type = TLV::Type::SerialNumber, .value = "IP01195230800010"},
                TLV{.type = TLV::Type::ManufactureDate, .value = "2023-02-23 06:12:51"},
                TLV{.type = TLV::Type::DeviceVersion, .value = uint8_t{0x21}},
                TLV{.type = TLV::Type::Vendor, .value = "SolidRun"},
                TLV{.type = TLV::Type::VendorExtension, .value = std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0x81, 0x04}},
            });
        }
        DOCTEST_SUBCASE("191_0-0052_eeprom.bin")
        {
            eepromFile = "191_0-0052_eeprom.bin";
            expected = TlvInfo({
                TLV{.type = TLV::Type::ProductName, .value = "Clearfog Base"},
                TLV{.type = TLV::Type::PartNumber, .value = "SRCFCBE000CV14"},
                TLV{.type = TLV::Type::SerialNumber, .value = "IP01195230800003"},
                TLV{.type = TLV::Type::ManufactureDate, .value = "2023-02-23 06:00:08"},
                TLV{.type = TLV::Type::DeviceVersion, .value = uint8_t{0x14}},
                TLV{.type = TLV::Type::Vendor, .value = "SolidRun"},
                TLV{.type = TLV::Type::VendorExtension, .value = std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0x81, 0x04}},
            });
        }
        DOCTEST_SUBCASE("191_0-0053_eeprom.bin")
        {
            eepromFile = "191_0-0053_eeprom.bin";
            expected = TlvInfo({
                TLV{.type = TLV::Type::ProductName, .value = "A38x SOM"},
                TLV{.type = TLV::Type::PartNumber, .value = "SRM6828S32D01GE008V21C0"},
                TLV{.type = TLV::Type::SerialNumber, .value = "IP01195230800003"},
                TLV{.type = TLV::Type::ManufactureDate, .value = "2023-02-23 06:00:08"},
                TLV{.type = TLV::Type::DeviceVersion, .value = uint8_t{0x21}},
                TLV{.type = TLV::Type::Vendor, .value = "SolidRun"},
                TLV{.type = TLV::Type::VendorExtension, .value = std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0x81, 0x04}},
            });
        }
        REQUIRE(velia::ietf_hardware::sysfs::onieEeprom(testsDir / eepromFile) == expected);
    }

    DOCTEST_SUBCASE("czechlight")
    {
        TlvInfo tlvs;
        std::string ftdiSN = "DQ000MPW";
        std::vector<uint8_t> opticalData {
                // version
                0x00,
                // eight bytes
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            };

        SECTION("one field")
        {
            tlvs = {
                {
                    .type = TLV::Type::VendorExtension,
                    .value = std::vector<uint8_t>{
                        // CESNET enterprise number
                        0x00, 0x00, 0x1f, 0x79,
                        // CzechLight version
                        0x00,

                        // length of the FTDI S/N
                        0x08,
                        // ...followed by the actual string
                        0x44, 0x51, 0x30, 0x30, 0x30, 0x4d, 0x50, 0x57,

                        // length of the optical calibration block
                        0x00, 0x09,
                        // ...which begins with a version magic byte
                        0x00,

                        // eight bytes of payload
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

                        // CRC32
                        0x02, 0x60, 0x51, 0x4b,
                    },
                },
            };
        }

        SECTION("two fields")
        {
            tlvs = {
                {
                    .type = TLV::Type::VendorExtension,
                    .value = std::vector<uint8_t>{
                        // CESNET enterprise number
                        0x00, 0x00, 0x1f, 0x79,
                        // CzechLight version
                        0x00,

                        // first part of the useful payload follows

                        // length of the FTDI S/N
                        0x08,
                        // ...followed by the actual string
                        0x44, 0x51, 0x30, 0x30, 0x30, 0x4d, 0x50, 0x57,

                        // length of the optical calibration block
                        0x00, 0x09,
                        // ...which begins with a version magic byte
                        0x00,

                        // eight bytes of payload
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

                        // CRC32 -- just first three bytes
                        0x02, 0x60, 0x51,
                    },
                },
                {
                    .type = TLV::Type::VendorExtension,
                    .value = std::vector<uint8_t>{
                        // CESNET enterprise number
                        0x00, 0x00, 0x1f, 0x79,
                        // CzechLight version
                        0x00,

                        // second part of the useful payload follows
                        0x4b
                    },
                }
            };
        }

        auto res = velia::ietf_hardware::sysfs::czechLightData(tlvs);
        REQUIRE(!!res);
        REQUIRE(res->ftdiSN == ftdiSN);
        REQUIRE(res->opticalData == opticalData);

        REQUIRE(!velia::ietf_hardware::sysfs::czechLightData(TlvInfo{}));
        REQUIRE(!velia::ietf_hardware::sysfs::czechLightData(TlvInfo{
            TLV{
                .type = TLV::Type::VendorExtension,
                .value = std::vector<uint8_t>{},
            },
            TLV{
                .type = TLV::Type::VendorExtension,
                .value = std::vector<uint8_t>{
                    // CESNET enterprise number
                    0x00, 0x00, 0x1f, 0x79,
                    // ... but no CzechLight version marker.
                },
            },
            TLV{
                .type = TLV::Type::VendorExtension,
                .value = std::vector<uint8_t>{
                    // some other party
                    0x01, 0x02, 0x03, 0x04,
                },
            },
        }));
    }

    DOCTEST_SUBCASE("Invalid files")
    {
        REQUIRE_THROWS_WITH_AS(velia::ietf_hardware::sysfs::onieEeprom(testsDir / "191_0-0053_eeprom-wrongcrc.bin"), "Failed to parse TlvInfo structure", std::runtime_error);
    }
}
