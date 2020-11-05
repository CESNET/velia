/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace velia::utils {

std::string readFileString(const std::filesystem::path& path);
std::vector<uint32_t> readFileBytes(const std::filesystem::path& path, int valuesCount);

}
