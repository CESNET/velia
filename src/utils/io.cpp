/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include <fstream>
#include "io.h"

namespace velia::utils {

namespace impl {

std::ifstream openStream(const std::filesystem::path& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open())
        throw std::invalid_argument("File '" + std::string(path) + "' does not exist.");
    return ifs;
}
}

/** @brief Reads a string from a file */
std::string readFileString(const std::filesystem::path& path)
{
    std::ifstream ifs(impl::openStream(path));
    std::string res;

    if (ifs >> res) {
        return res;
    }

    throw std::domain_error("Could not read '" + std::string(path) + "'.");
}

/** @brief Reads @p valuesCount 32bit values from a file */
std::vector<uint32_t> readFileWords(const std::filesystem::path& path, int valuesCount)
{
    std::ifstream ifs(impl::openStream(path));
    std::vector<uint32_t> bytes; // no interesting data are wider than 32 bits (for now)

    if (!ifs.is_open()) {
        throw std::invalid_argument("File '" + std::string(path) + "' does not exist.");
    }

    uint32_t byte;
    ifs >> std::hex;
    while (valuesCount-- && ifs >> byte) {
        bytes.push_back(byte);
    }

    if (!ifs) {
        throw std::domain_error("Could not read hex data from '" + std::string(path) + "'.");
    }

    return bytes;
}

}
