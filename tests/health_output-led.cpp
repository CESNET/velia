/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include <fstream>
#include "health/outputs/LedSysfsDriver.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

namespace {

uint32_t readFile(const std::filesystem::path& path)
{
    std::ifstream ifs(path);
    uint32_t ret;
    if (!(ifs >> ret)) {
        throw std::invalid_argument("Failed reading '" + std::string(path) + "'.");
    }
    return ret;
}

/** @short Remove directory tree at 'rootDir' path (if exists) */
void removeDirectoryTreeIfExists(const std::filesystem::path& rootDir)
{
    if (std::filesystem::exists(rootDir)) {
        std::filesystem::remove_all(rootDir);
    }
}

}

TEST_CASE("SysFS LED driver")
{
    using namespace std::literals;
    TEST_INIT_LOGS;

    auto fakeSysfsDir = std::filesystem::path {CMAKE_CURRENT_BINARY_DIR + "/tests/health_output-led/"s};
    auto fakeBrightnessFile = fakeSysfsDir / "brightness";
    removeDirectoryTreeIfExists(fakeSysfsDir);

    SECTION("Basic usage test")
    {
        std::filesystem::copy(std::string(CMAKE_CURRENT_SOURCE_DIR) + "/tests/sysfs/leds/line:green/"s, fakeSysfsDir, std::filesystem::copy_options::recursive);
        velia::health::LedSysfsDriver led(fakeSysfsDir);

        led.set(0);
        REQUIRE(readFile(fakeBrightnessFile) == 0);

        led.set(1);
        REQUIRE(readFile(fakeBrightnessFile) == 1);

        led.set(42);
        REQUIRE(readFile(fakeBrightnessFile) == 42);

        led.set(0);
        REQUIRE(readFile(fakeBrightnessFile) == 0);
    }

    SECTION("Invalid directory")
    {
        std::filesystem::copy(std::string(CMAKE_CURRENT_SOURCE_DIR) + "/tests/sysfs/leds/line:red/"s, fakeSysfsDir, std::filesystem::copy_options::recursive);
        REQUIRE_THROWS_AS(velia::health::LedSysfsDriver(fakeSysfsDir), std::invalid_argument);
    }
}
