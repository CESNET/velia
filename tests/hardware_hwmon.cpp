/*
 * Copyright (C) 2017-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include <fstream>
#include "pretty_printers.h"
#include "sysfs/Exceptions.h"
#include "sysfs/HWMon.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

namespace {

/** @short Represents a temporary file whose lifetime is bound by lifetime of the FileInjector instance */
class FileInjector {
private:
    const std::string path;

public:
    /** @short Creates a file with specific permissions and content */
    FileInjector(const std::string& path, const std::filesystem::perms permissions, const std::string& content)
        : path(path)
    {
        auto fileStream = std::ofstream(path, std::ios_base::out | std::ios_base::trunc);
        if (!fileStream.is_open()) {
            throw velia::hardware::sysfs::Error("FileInjector could not open file " + path + " for writing");
        }
        fileStream << content;
        std::filesystem::permissions(path, permissions);
    }
    /** @short Removes file associated with this FileInjector instance (if exists) */
    ~FileInjector() noexcept(false)
    {
        std::filesystem::remove(path);
    }

    /** @short Sets file permissions */
    void setPermissions(const std::filesystem::perms permissions)
    {
        std::filesystem::permissions(path, permissions);
    }
};

/** @short Remove directory tree at 'rootDir' path (if exists) */
void removeDirectoryTreeIfExists(const std::string& rootDir)
{
    if (std::filesystem::exists(rootDir)) {
        std::filesystem::remove_all(rootDir);
    }
}
}

TEST_CASE("HWMon class")
{
    TEST_INIT_LOGS;

    const auto fakeHwmonRoot = CMAKE_CURRENT_BINARY_DIR + "/tests/hwmon/"s;
    removeDirectoryTreeIfExists(fakeHwmonRoot);

    SECTION("Test hwmon/device1")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);
        auto hwmon = velia::hardware::sysfs::HWMon(fakeHwmonRoot);
        std::map<std::string, int64_t> expected({
            {"temp1_crit", 105'000},
            {"temp1_input", 66'600},
            {"temp2_crit", 105'000},
            {"temp2_input", 29'800},
            {"temp10_crit", 666'777},
            {"temp10_input", 66'600},
            {"temp11_input", 111'222'333'444'555},
        });

        REQUIRE(hwmon.attributes() == expected);
    }

    SECTION("Test hwmon/device1 + one of the files unreadable")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        // Inject temporary file for "no read permission" test
        auto injected_noread = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_write, "-42001");

        auto hwmon = velia::hardware::sysfs::HWMon(fakeHwmonRoot);
        std::map<std::string, int64_t> expected({
            {"temp1_crit", 105'000},
            {"temp1_input", 66'600},
            {"temp2_crit", 105'000},
            {"temp2_input", 29'800},
            {"temp3_input", -42'001},
            {"temp10_crit", 666'777},
            {"temp10_input", 66'600},
            {"temp11_input", 111'222'333'444'555},
        });

        // no read permission now
        REQUIRE_THROWS_AS(hwmon.attributes(), velia::hardware::sysfs::Error);

        // read permission granted
        injected_noread->setPermissions(std::filesystem::perms::owner_all);
        REQUIRE(hwmon.attributes() == expected);
    }

    SECTION("Test hwmon/device1 + one of the files disappears after construction")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        // Inject temporary file for "file does not exist" test
        auto injected_notexist = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "-42001");

        auto hwmon = velia::hardware::sysfs::HWMon(fakeHwmonRoot);

        // file exists now
        std::map<std::string, int64_t> expected({
            {"temp1_crit", 105'000},
            {"temp1_input", 66'600},
            {"temp2_crit", 105'000},
            {"temp2_input", 29'800},
            {"temp3_input", -42'001},
            {"temp10_crit", 666'777},
            {"temp10_input", 66'600},
            {"temp11_input", 111'222'333'444'555},
        });

        // file exists, should be OK
        REQUIRE(hwmon.attributes() == expected);

        // file deleted
        injected_notexist.reset();
        REQUIRE_THROWS_AS(hwmon.attributes(), velia::hardware::sysfs::FileDoesNotExist);
    }

    SECTION("Test hwmon/device1 + invalid values")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device1/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        SECTION("Invalid content")
        {
            auto injected = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "cus bus");
            auto hwmon = velia::hardware::sysfs::HWMon(fakeHwmonRoot);
            REQUIRE_THROWS_AS(hwmon.attributes(), velia::hardware::sysfs::ParseError);
        }

        SECTION("Invalid value range")
        {
            auto injected = std::make_unique<FileInjector>(fakeHwmonRoot + "/hwmon0/temp3_input", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "-99999999999999999999999999999999");
            auto hwmon = velia::hardware::sysfs::HWMon(fakeHwmonRoot);
            REQUIRE_THROWS_AS(hwmon.attributes(), velia::hardware::sysfs::ParseError);
        }
    }

    SECTION("Test hwmon/device2")
    {
        std::filesystem::copy(CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/hwmon/device2/hwmon"s, fakeHwmonRoot, std::filesystem::copy_options::recursive);

        auto hwmon = velia::hardware::sysfs::HWMon(fakeHwmonRoot);
        std::map<std::string, int64_t> expected({
            {"temp1_crit", 9'223'372'036'854'775'807LL},
            {"temp1_input", -34'000},
            {"temp1_max", 80'000},
            // LOL, -9223372036854775808LL can not be used because it consists of two tokens: -, and 9223372036854775808LL. Therefore it produces error: integer constant is so large that it is unsigned
            // See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52661
            // Yes, using std::numeric_limits<int64_t>::min() would work but I wanted to make clear that *this* is the contents. Not some symbolic name.
            {"temp2_crit", -9'223'372'036'854'775'807LL - 1},
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
        });

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

        REQUIRE_THROWS_AS(velia::hardware::sysfs::HWMon(fakeHwmonRoot), velia::hardware::sysfs::Error);
    }
}
