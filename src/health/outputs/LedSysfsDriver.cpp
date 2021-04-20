/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include <fstream>
#include "LedSysfsDriver.h"
#include "utils/io.h"
#include "utils/log.h"

namespace velia::health {

LedSysfsDriver::LedSysfsDriver(const std::filesystem::path& directory)
    : m_log(spdlog::get("health"))
    , m_brightnessFile(directory / "brightness")
{
    // check that brightness file exists
    if (!std::filesystem::exists(m_brightnessFile)) {
        throw std::invalid_argument("Sysfs dir must contain 'brightness' file.");
    }

    const auto maxBrightnessFile = directory / "max_brightness";
    if (!std::filesystem::exists(maxBrightnessFile)) {
        throw std::invalid_argument("Sysfs dir must contain 'max_brightness' file.");
    }
    m_maxBrightness = utils::readFileInt64(maxBrightnessFile);

    m_log->trace("Initialized LED {}", std::string(directory));
}

/** @brief Set the brightness of the LED to @p brightness.
 *  Caller is responsible for providing correct brightness value. No checks for valid value are performed.
 */
void LedSysfsDriver::set(uint32_t brightness)
{
    std::ofstream ofs(m_brightnessFile);
    if (!(ofs << brightness)) {
        throw std::invalid_argument("Write to '" + std::string(m_brightnessFile) + "' failed.");
    }
}
uint32_t LedSysfsDriver::getMaxBrightness() const
{
    return m_maxBrightness;
}

}
