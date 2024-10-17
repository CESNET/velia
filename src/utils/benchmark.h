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
#define VELIA_UTILS_PASTE_INNER(X, Y) X##Y
#define VELIA_UTILS_PASTE(X, Y) VELIA_UTILS_PASTE_INNER(X, Y)
#define WITH_TIME_MEASUREMENT ::velia::utils::MeasureTime VELIA_UTILS_PASTE(_benchmark_,  __LINE__)
