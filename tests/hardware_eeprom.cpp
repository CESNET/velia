/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "fs-helpers/utils.h"
#include "ietf-hardware/sysfs/EEPROM.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("EEPROM reader")
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
