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
#include "ietf-hardware/sysfs/EEPROM.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("EEPROM with UID/EID")
{
    using namespace velia::ietf_hardware;
    using namespace data_reader;
    TEST_INIT_LOGS;
    const auto sysfs = CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/"s;

    REQUIRE(*hexEEPROM(sysfs, 1, 0x5c, 16, 0, 16) == "1E70C61C941000628C2EA000A000000C");

    auto working = EepromWithUid("x:eeprom", "x", sysfs, 0, 0x52, 256, 256 - 6, 6);
    REQUIRE(working().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/serial-num", "294100B13DA3"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "enabled"},
            });

    auto missing = EepromWithUid("x:eeprom", "x", sysfs, 0, 0x53, 256, 256 - 6, 6);
    REQUIRE(missing().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "disabled"},
            });

    auto corrupted = EepromWithUid("x:eeprom", "x", sysfs, 0, 0x53, 16, 2, 6);
    REQUIRE(corrupted().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "disabled"},
            });

    REQUIRE_THROWS_WITH(EepromWithUid("x:eeprom", "x", sysfs, 0, 0x20, 256, 256 - 6, 6),
                        "EEPROM: no I2C device defined at bus 0 address 0x20");

    REQUIRE_THROWS_WITH(hexEEPROM(sysfs, 0, 0, 10, 5, 6),
                        "EEPROM: region out of range");
}

TEST_CASE("IPMI EEPROM reader")
{
    using velia::ietf_hardware::sysfs::CommonHeader;
    using velia::ietf_hardware::sysfs::FRUInformationStorage;
    using velia::ietf_hardware::sysfs::ProductInfo;

    TEST_INIT_LOGS;

    const auto testsDir = std::filesystem::path{CMAKE_CURRENT_SOURCE_DIR} / "tests" / "sysfs" / "eeprom"s;

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

        REQUIRE(velia::ietf_hardware::sysfs::eepromData(testsDir / eepromFile) == expected);
    }

    DOCTEST_SUBCASE("Invalid files")
    {
        std::string exception;

        DOCTEST_SUBCASE("Wrong product area length")
        {
            DOCTEST_SUBCASE("1")
            {
                eepromFile = "SDN-ID210512_eeprom-2-0050_wrong_prodarea_len.bin";
                exception = "Padding: child parser already read 83 bytes, but the total length including padding is expected to be 80";
            }
            DOCTEST_SUBCASE("2")
            {
                eepromFile = "SDN-ID210512_eeprom-2-0051_wrong_prodarea_len.bin";
                exception = "Padding: child parser already read 83 bytes, but the total length including padding is expected to be 80";
            }
        }
        DOCTEST_SUBCASE("Wrong header checksum")
        {
            eepromFile = "wrong_header_checksum.bin";
            exception = "Failed to parse EEPROM data Common Header";
        }
        DOCTEST_SUBCASE("format is not 0x01, correct checksum")
        {
            eepromFile = "wrong_header_format.bin";
            exception = "Failed to parse EEPROM data Common Header";
        }
        DOCTEST_SUBCASE("pad is not 0x00, correct checksum")
        {
            eepromFile = "wrong_header_pad.bin";
            exception = "Failed to parse EEPROM data Common Header";
        }

        REQUIRE_THROWS_WITH_AS(velia::ietf_hardware::sysfs::eepromData(testsDir / eepromFile), exception.c_str(), std::runtime_error);
    }
}
