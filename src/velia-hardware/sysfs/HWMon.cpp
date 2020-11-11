/*
 * Copyright (C) 2017-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 *
*/
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "sysfs/Exceptions.h"
#include "sysfs/HWMon.h"
#include "utils/log.h"
#include "utils/string.h"

namespace velia::hardware::sysfs {

using namespace std::literals;

/** @class HWMon

@short Implements access to sensor chips data in specific hwmon directory.

This class provides property-like access to various sensoric data from kernel hwmon subsystem.

Docs: https://www.kernel.org/doc/Documentation/hwmon/sysfs-interface
Kernel: https://github.com/torvalds/linux/tree/master/drivers/hwmon
*/


namespace impl {

/** @brief Reads a number from a hwmon file.
 *
 * int64_t was chosen because kernel seems to print numeric values as signed long ints (@see linux/drivers/hwmon/hwmon.c)
 */
int64_t readFile(const std::filesystem::path& path)
{
    std::ifstream ifs(path);
    int64_t res;

    if (!ifs.is_open())
        throw FileDoesNotExist("File '" + std::string(path) + "' does not exist.");

    if (ifs >> res) {
        return res;
    }

    throw ParseError("Could not read '" + std::string(path) + "'.");
}

/** @brief Only files from hwmon directory that end with these suffixes are considered */
static const std::vector<std::string> ACCEPTED_FILE_ENDINGS {
    "_input",
    "_crit",
    "_min",
    "_max",
    "_average",
    "_highest",
    "_lowest",
};
}

/**
 * @short Constructs a HWMon driver for hwmon entries
 * @param root A path to the hwmon using specific device directory from /sys/devices/ or /sys/bus/i2c, e.g.: /sys/devices/platform/soc/soc:internal-regs/f1011100.i2c/i2c-1/1-002e/hwmon or /sys/bus/i2c/devices/2-0025/hwmon
 * */
HWMon::HWMon(std::filesystem::path hwmonDir)
    : m_log(spdlog::get("hardware"))
{
    // Find root directory (should be called hwmonX)
    std::vector<std::filesystem::path> rootCandidates;
    if (std::filesystem::exists(hwmonDir)) {
        for (const auto& e : std::filesystem::directory_iterator(hwmonDir)) {
            // only directories hwmon<int> with a file named 'name' (required by kernel docs) are valid
            if (e.is_directory() && velia::utils::startsWith(e.path().filename(), "hwmon") && std::filesystem::exists(e.path() / "name"s)) {
                rootCandidates.push_back(e.path());
                m_log->trace("hwmon: Found a candidate: {}", std::string(e.path()));
            }
        }
    }

    if (rootCandidates.size() != 1)
        throw Error("Invalid hwmon directory ('" + std::string(hwmonDir) + "')");
    m_root = rootCandidates.at(0);

    m_log->trace("HWMon() driver initialized for '{}'", std::string(m_root));

    // Scan through files in root directory, discard directories, non-readable files and non-interesting (see accepted_endings) files
    for (const auto& entry : std::filesystem::directory_iterator(m_root)) {
        if (!std::filesystem::is_regular_file(entry.path())) {
            continue;
        }

        if (std::any_of(impl::ACCEPTED_FILE_ENDINGS.cbegin(), impl::ACCEPTED_FILE_ENDINGS.cend(), [&entry](const auto& ending) { return velia::utils::endsWith(entry.path().filename(), ending); })) {
            m_properties.push_back(entry.path().filename());
        }
    }
}

HWMon::~HWMon() = default;

/** @brief Return attributes read by this hwmon.
 *
 * For the return value discussion @see HWMon::readFile
 */
std::map<std::string, int64_t> HWMon::attributes() const
{
    std::map<std::string, int64_t> result;

    for (const auto& propertyName : m_properties) {
        result.insert(std::make_pair(propertyName, impl::readFile(m_root / propertyName)));
    }

    return result;
}
}
