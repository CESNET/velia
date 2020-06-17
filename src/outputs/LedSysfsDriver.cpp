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
    // check dir/brightness exists
    if (!std::filesystem::exists(m_brightnessFile)) {
        throw std::invalid_argument("Sysfs dir must contain 'brightness' file.");
    }

    // check dir/max_brightness exists, read max brightness value
    std::filesystem::path max_brightness = directory / "max_brightness";
    if (!std::filesystem::exists(max_brightness)) {
        throw std::invalid_argument("Sysfs dir must contain 'max_brightness' file.");
    }

    std::ifstream ifs(max_brightness);
    if (!(ifs >> m_maxBrightness)) {
        throw std::invalid_argument("Failed reading 'max_brightness' file.");
    }

    m_log->trace("Initialized LED {}, max_brightness is 1", std::string(directory), std::to_string(m_maxBrightness));
}

uint32_t LedSysfsDriver::getMaxBrightness() const
{
    return m_maxBrightness;
}

void LedSysfsDriver::off()
{
    writeBrightnessValue(0);
}

void LedSysfsDriver::on()
{
    writeBrightnessValue(m_maxBrightness);
}

void LedSysfsDriver::on(uint32_t brightness)
{
    writeBrightnessValue(brightness);
}

void LedSysfsDriver::writeBrightnessValue(uint32_t value)
{
    if (value > m_maxBrightness) {
        m_log->warn("Selected brightness of {} is more than maximum ({} > {}). Setting to {}.", std::string(m_brightnessFile), value, m_maxBrightness, m_maxBrightness);
        value = m_maxBrightness;
    }

    std::ofstream ofs(m_brightnessFile);
    if (!(ofs << value)) {
        throw std::invalid_argument("Write to '" + std::string(m_brightnessFile) + "' failed.");
    }
}

}