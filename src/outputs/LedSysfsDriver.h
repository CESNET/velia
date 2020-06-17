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
 * @brief Sysfs driver for manipulating with LED brightness using https://www.kernel.org/doc/Documentation/leds/leds-class.txt
 */
class LedSysfsDriver {
public:
    explicit LedSysfsDriver(const std::filesystem::path& directory);
    void off();
    void set(uint32_t brightness);

private:
    velia::Log m_log;

    /** path to the brightness file */
    std::filesystem::path m_brightnessFile;

    void writeBrightness(uint32_t value);
};

}