/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <chrono>
#include <string>

namespace velia::utils {

std::string yangTimeFormat(const std::chrono::time_point<std::chrono::system_clock>& timePoint);
}
