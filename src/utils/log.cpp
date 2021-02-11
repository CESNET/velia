/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <docopt.h>
#include "log.h"

spdlog::level::level_enum parseLogLevel(const std::string& name, const docopt::value& option)
{
    long x;
    try {
        x = option.asLong();
    } catch (std::invalid_argument&) {
        throw std::runtime_error(name + " log level: expecting integer");
    }
    static_assert(spdlog::level::trace < spdlog::level::off, "spdlog::level levels have changed");
    static_assert(spdlog::level::off == 6, "spdlog::level levels have changed");
    if (x < 0 || x > 5)
        throw std::runtime_error(name + " log level invalid or out-of-range");

    return static_cast<spdlog::level::level_enum>(5 - x);
}
