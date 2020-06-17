/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include <fstream>
#include "LedSysfsDriver.h"
#include "utils/log.h"

namespace velia {

LedSysfsDriver::LedSysfsDriver(const std::filesystem::path& directory)
    : m_log(spdlog::get("output"))
    , m_brightnessFile(directory / "brightness")
{
    // check that brightness file exists
    if (!std::filesystem::exists(m_brightnessFile)) {
        throw std::invalid_argument("Sysfs dir must contain 'brightness' file.");
    }

    m_log->trace("Initialized LED {}", std::string(directory));
}

/** @brief Turn the LED off by writing 0 into brightness file */
void LedSysfsDriver::off()
{
    writeBrightness(0);
}

/** @brief Set the brightness of the LED to @p brightness.
 *  Caller is responsible for providing correct brightness value. There are no checks whether the brightness is in the valid range.
 */
void LedSysfsDriver::set(uint32_t brightness)
{
    writeBrightness(brightness);
}

void LedSysfsDriver::writeBrightness(uint32_t value)
{
    std::ofstream ofs(m_brightnessFile);
    if (!(ofs << value)) {
        throw std::invalid_argument("Write to '" + std::string(m_brightnessFile) + "' failed.");
    }
}

}