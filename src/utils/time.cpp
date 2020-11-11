/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <iomanip>
#include <sstream>
#include <utils/time.h>

/** @short Utilitary functions for various needs */
namespace velia::utils {

/** @brief Converts a time_point to a UTC timezone textual representation required by yang:date-and-time. */
std::string yangTimeFormat(const std::chrono::time_point<std::chrono::system_clock>& timePoint)
{
    auto time = std::chrono::system_clock::to_time_t(timePoint);

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}
}
