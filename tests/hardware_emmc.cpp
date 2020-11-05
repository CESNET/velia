/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "ietf-hardware/sysfs/EMMC.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

namespace {

/** @short Remove directory tree at 'rootDir' path (if exists) */
void removeDirectoryTreeIfExists(const std::string& rootDir)
{
    if (std::filesystem::exists(rootDir)) {
        std::filesystem::remove_all(rootDir);
    }
}
}

TEST_CASE("EMMC driver")
{
    TEST_INIT_LOGS;

    const auto fakeRoot = CMAKE_CURRENT_BINARY_DIR + "/tests/emmc/"s;
    removeDirectoryTreeIfExists(fakeRoot);

    SECTION("Test correct structure")
    {
        std::string sourceDir;
        velia::ietf_hardware::sysfs::EMMCAttributes expected;

        SECTION("device1")
        {
            sourceDir = CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/emmc/device1"s;
            expected = {
                {"date", "02/2015"},
                {"serial", "0x00a8808d"},
                {"name", "8GME4R"},
                // life_time: 0x01 0x02 (i.e., 0-10% and 10-20%)
                // pre_eol_info: 0x01 (i.e., normal)
                {"life_time", "10"}};
        }

        SECTION("device2")
        {
            sourceDir = CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/emmc/device2"s;
            expected = {
                {"date", "02/2015"},
                {"serial", "0x00a8808d"},
                {"name", "8GME4R"},
                // life_time: 0x0B 0x02 (i.e., 100-?% and 10-20%)
                // pre_eol_info: 0x03 (i.e., urgent)
                {"life_time", "100"}};
        }

        std::filesystem::copy(sourceDir, fakeRoot, std::filesystem::copy_options::recursive);
        auto emmcAttrs = velia::ietf_hardware::sysfs::EMMC(fakeRoot);
        REQUIRE(emmcAttrs.attributes() == expected);
    }

    SECTION("Test emmc (<5) / device3")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/emmc/device3"s, fakeRoot, std::filesystem::copy_options::recursive);

        // health reporting missing (emmc < 5). When one file is missing, the attributes method invocation throws
        REQUIRE_THROWS_AS(velia::ietf_hardware::sysfs::EMMC(fakeRoot).attributes(), std::invalid_argument);
    }
}
