/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
*/

#include "benchmark.h"
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::literals;

namespace velia::utils {
    MeasureTime::MeasureTime(const std::source_location location)
    : start(std::chrono::steady_clock::now())
    , what(location.function_name())
{
    if (auto fn = location.function_name(); fn != ""sv) {
        what = fn;
    } else {
        what = fmt::format("{}:{}:{}", location.file_name(), location.line(), location.column());
    }
}

MeasureTime::MeasureTime(const std::string_view message)
    : start(std::chrono::steady_clock::now())
    , what(message)
{
}

MeasureTime::~MeasureTime()
{
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (ms > 1'000) {
        spdlog::warn("[PERFORMANCE][TOO_SLOW] {} {}ms", what, ms);
    } else {
        spdlog::trace("[PERFORMANCE]: {} {}ms", what, ms);
    }
}
}
