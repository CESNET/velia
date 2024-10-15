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
int64_t readFileInt64(const std::filesystem::path& path);
std::vector<uint32_t> readFileWords(const std::filesystem::path& path, int valuesCount);
std::string readFileToString(const std::filesystem::path& path);
std::vector<uint8_t> readFileToBytes(const std::filesystem::path& path);
void writeFile(const std::string& path, const std::string_view& contents);
void safeWriteFile(const std::string& filename, const std::string_view& contents);
}
