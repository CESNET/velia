/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
*/

#pragma once

#include <chrono>
#include <string_view>
#include <source_location>

namespace velia::utils {
/** @short Log profiling information about how much time was spent in a given block */
class MeasureTime {
    std::chrono::time_point<std::chrono::steady_clock> start;
    std::string what;
public:
    MeasureTime(const std::source_location location = std::source_location::current());
    MeasureTime(const std::string_view message);
    ~MeasureTime();
};
}
#define WITH_TIME_MEASUREMENT ::velia::utils::MeasureTime _benchmark_ ## __LINE__{}
