/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace velia::utils {

std::string readFileString(const std::filesystem::path& path);
std::vector<uint32_t> readFileWords(const std::filesystem::path& path, int valuesCount);
std::string readFileToString(const std::filesystem::path& path);
void safeWriteFile(const std::string& filename, const std::string_view& contents);
std::ifstream openStream(const std::filesystem::path& path);

/** @brief Reads from a file using operator>> for given template type T */
template <class T>
T readOneFromFile(const std::filesystem::path& path)
{
    std::ifstream ifs(openStream(path));
    T res;

    if (ifs >> res) {
        return res;
    }

    throw std::domain_error("Could not read value from '" + std::string(path) + "'.");
}
}
