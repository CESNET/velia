/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include <fstream>
#include "outputs/LedSysfsDriver.h"
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

    auto fakeSysfsDir = std::filesystem::path {CMAKE_CURRENT_BINARY_DIR + "/tests/led/"s};
    auto fakeBrightnessFile = fakeSysfsDir / "brightness";
    auto fakeMaxBrightnessFile = fakeSysfsDir / "max_brightness";
    removeDirectoryTreeIfExists(fakeSysfsDir);

    SECTION("test led with 0-1 brightness")
    {
        std::filesystem::copy(std::string(CMAKE_CURRENT_SOURCE_DIR) + "/tests/sysfs/led/1/"s, fakeSysfsDir, std::filesystem::copy_options::recursive);
        velia::LedSysfsDriver led(fakeSysfsDir);

        REQUIRE(led.getMaxBrightness() == 1);
        led.off();
        REQUIRE(readFile(fakeBrightnessFile) == 0);

        led.on();
        REQUIRE(readFile(fakeBrightnessFile) == 1);

        led.off();
        REQUIRE(readFile(fakeBrightnessFile) == 0);
    }

    SECTION("test led with 0-255 brightness")
    {
        std::filesystem::copy(std::string(CMAKE_CURRENT_SOURCE_DIR) + "/tests/sysfs/led/2/"s, fakeSysfsDir, std::filesystem::copy_options::recursive);
        velia::LedSysfsDriver led(fakeSysfsDir);

        REQUIRE(led.getMaxBrightness() == 255);
        led.off();
        REQUIRE(readFile(fakeBrightnessFile) == 0);

        led.on();
        REQUIRE(readFile(fakeBrightnessFile) == 255);

        led.off();
        REQUIRE(readFile(fakeBrightnessFile) == 0);

        led.on(166);
        REQUIRE(readFile(fakeBrightnessFile) == 166);

        // can't write more than max
        led.on(300);
        REQUIRE(readFile(fakeBrightnessFile) == 255);
    }

    SECTION("invalid directory")
    {
        std::filesystem::copy(std::string(CMAKE_CURRENT_SOURCE_DIR) + "/tests/sysfs/led/3/"s, fakeSysfsDir, std::filesystem::copy_options::recursive);
        REQUIRE_THROWS_AS(velia::LedSysfsDriver(fakeSysfsDir), std::invalid_argument);
    }
}
