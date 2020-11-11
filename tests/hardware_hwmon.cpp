/*
 * Copyright (C) 2017-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include <fstream>
#include "fs-helpers/FileInjector.h"
#include "fs-helpers/utils.h"
#include "ietf-hardware/sysfs/HWMon.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("HWMon class")
{
    TEST_INIT_LOGS;

    const auto fakeHwmonRoot = CMAKE_CURRENT_BINARY_DIR + "/tests/hwmon/"s;
    removeDirectoryTreeIfExists(fakeHwmonRoot);
    velia::ietf_hardware::sysfs::HWMon::Attributes expected;

    SECTION("Test hwmon/device1")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);
        auto hwmon = velia::ietf_hardware::sysfs::HWMon(fakeHwmonRoot);
        expected = {
            {"temp1_crit", 105'000},
            {"temp1_input", 66'600},
            {"temp2_crit", 105'000},
            {"temp2_input", 29'800},
            {"temp10_crit", 666'777},
            {"temp10_input", 66'600},
            {"temp11_input", 111'222'333'444'555},
        };

        REQUIRE(hwmon.attributes() == expected);
    }

    SECTION("Test hwmon/device1 + one of the files unreadable")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        // Inject temporary file for "no read permission" test
        auto injected_noread = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_write, "-42001");

        auto hwmon = velia::ietf_hardware::sysfs::HWMon(fakeHwmonRoot);
        expected = {
            {"temp1_crit", 105'000},
            {"temp1_input", 66'600},
            {"temp2_crit", 105'000},
            {"temp2_input", 29'800},
            {"temp3_input", -42'001},
            {"temp10_crit", 666'777},
            {"temp10_input", 66'600},
            {"temp11_input", 111'222'333'444'555},
        };

        // no read permission now
        REQUIRE_THROWS_AS(hwmon.attributes(), std::invalid_argument);

        // read permission granted
        injected_noread->setPermissions(std::filesystem::perms::owner_all);
        REQUIRE(hwmon.attributes() == expected);
    }

    SECTION("Test hwmon/device1 + one of the files disappears after construction")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        // Inject temporary file for "file does not exist" test
        auto injected_notexist = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "-42001");

        auto hwmon = velia::ietf_hardware::sysfs::HWMon(fakeHwmonRoot);

        expected = {
            {"temp1_crit", 105'000},
            {"temp1_input", 66'600},
            {"temp2_crit", 105'000},
            {"temp2_input", 29'800},
            {"temp3_input", -42'001},
            {"temp10_crit", 666'777},
            {"temp10_input", 66'600},
            {"temp11_input", 111'222'333'444'555},
        };

        // file exists, should be OK
        REQUIRE(hwmon.attributes() == expected);

        // file deleted
        injected_notexist.reset();
        REQUIRE_THROWS_AS(hwmon.attributes(), std::invalid_argument);
    }

    SECTION("Test hwmon/device1 + invalid values")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        SECTION("Invalid content")
        {
            auto injected = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "cus bus");
            auto hwmon = velia::ietf_hardware::sysfs::HWMon(fakeHwmonRoot);
            REQUIRE_THROWS_AS(hwmon.attributes(), std::domain_error);
        }

        SECTION("Invalid value range")
        {
            auto injected = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "-99999999999999999999999999999999");
            auto hwmon = velia::ietf_hardware::sysfs::HWMon(fakeHwmonRoot);
            REQUIRE_THROWS_AS(hwmon.attributes(), std::domain_error);
        }
    }

    SECTION("Test hwmon/device2")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device2/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        auto hwmon = velia::ietf_hardware::sysfs::HWMon(fakeHwmonRoot);
        expected = {
            {"temp1_crit", std::numeric_limits<int64_t>::max()},
            {"temp1_input", -34'000},
            {"temp1_max", 80'000},
            {"temp2_crit", std::numeric_limits<int64_t>::min()}, // we can't write an integer literal for int64_t min value (see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52661)
            {"temp2_input", -34'000},
            {"temp2_max", 80'000},
            {"temp3_crit", 100'000},
            {"temp3_input", 30'000},
            {"temp3_max", 80'000},
            {"temp4_crit", 100'000},
            {"temp4_input", 26'000},
            {"temp4_max", 80'000},
            {"temp5_crit", 100'000},
            {"temp5_input", 29'000},
            {"temp5_max", 80'000},
        };

        REQUIRE(hwmon.attributes() == expected);
    }

    SECTION("Test wrong directory structure")
    {
        std::string sourceDir;
        SECTION("No hwmonX directory")
        {
            sourceDir = "tests/sysfs/hwmon/device4/hwmon"s;
        }
        SECTION("Multiple hwmonX directories")
        {
            sourceDir = "tests/sysfs/hwmon/device3/hwmon"s;
        }

        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/"s + sourceDir, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        REQUIRE_THROWS_AS(velia::ietf_hardware::sysfs::HWMon(fakeHwmonRoot), std::invalid_argument);
    }
}
