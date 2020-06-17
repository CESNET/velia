/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#pragma once

#include <filesystem>
#include "utils/log-fwd.h"

namespace velia {

/**
 * @brief Sysfs driver for manipulating with LED brightness
 */
class LedSysfsDriver {
public:
    explicit LedSysfsDriver(const std::filesystem::path& directory);
    void off();
    void on();
    void on(uint32_t brightness);
    uint32_t getMaxBrightness() const;


private:
    velia::Log m_log;
    std::filesystem::path m_brightnessFile;
    uint32_t m_maxBrightness;

    void writeBrightnessValue(uint32_t value);
};

}