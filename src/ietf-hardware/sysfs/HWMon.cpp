/*
 * Copyright (C) 2017-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#include <algorithm>
#include <filesystem>
#include "HWMon.h"
#include "utils/io.h"
#include "utils/log.h"

namespace velia::ietf_hardware::sysfs {

using namespace std::literals;

/** @class HWMon

@short Implements access to sensor chips data in specific hwmon directory.

This class provides property-like access to various sensoric data from kernel hwmon subsystem.

Docs: https://www.kernel.org/doc/Documentation/hwmon/sysfs-interface
Kernel: https://github.com/torvalds/linux/tree/master/drivers/hwmon
*/


namespace {

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
            if (e.is_directory() && std::string(e.path().filename()).starts_with("hwmon") && std::filesystem::exists(e.path() / "name"s)) {
                rootCandidates.push_back(e.path());
                m_log->trace("hwmon: Found a candidate: {}", std::string(e.path()));
            }
        }
    }

    if (rootCandidates.size() != 1)
        throw std::invalid_argument("Invalid hwmon directory ('" + std::string(hwmonDir) + "')");
    m_root = rootCandidates.front();

    m_log->trace("HWMon() driver initialized for '{}'", std::string(m_root));

    // Scan through files in root directory, discard directories, non-readable files and non-interesting (see accepted_endings) files
    for (const auto& entry : std::filesystem::directory_iterator(m_root)) {
        if (!std::filesystem::is_regular_file(entry.path())) {
            continue;
        }

        if (std::any_of(ACCEPTED_FILE_ENDINGS.cbegin(), ACCEPTED_FILE_ENDINGS.cend(), [&entry](const auto& ending) { return std::string(entry.path().filename()).ends_with(ending); })) {
            m_properties.push_back(entry.path().filename());
        }
    }
}

HWMon::~HWMon() = default;

/** @brief Return attributes read by this hwmon.
 *
 * For the return value discussion @see HWMon::readFile
 */
HWMon::Attributes HWMon::attributes() const
{
    Attributes result;

    for (const auto& propertyName : m_properties) {
        // Read int64_t value because kernel seems to print numeric values as signed long ints (@see linux/drivers/hwmon/hwmon.c)
        result.insert(std::make_pair(propertyName, velia::utils::readFileInt64(m_root / propertyName)));
    }

    return result;
}

/** @brief Returns one attribute.  */
int64_t HWMon::attribute(const std::string& propertyName) const
{
    Attributes result;
    if (std::find(m_properties.begin(), m_properties.end(), propertyName) == m_properties.end()) {
        throw std::invalid_argument("hwmon: attribute '" + propertyName + "' doesn't exist.");
    }

    return velia::utils::readFileInt64(m_root / propertyName);
}

}
