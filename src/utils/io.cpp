/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include <cstring>
#include <fmt/core.h>
#include <fstream>
#include <unistd.h>
#include "io.h"

namespace velia::utils {

namespace {

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
    std::ifstream ifs(openStream(path));
    std::string res;

    if (ifs >> res) {
        return res;
    }

    throw std::domain_error("Could not read '" + std::string(path) + "'.");
}

/** @brief Reads @p valuesCount 32bit values from a file */
std::vector<uint32_t> readFileWords(const std::filesystem::path& path, int valuesCount)
{
    std::ifstream ifs(openStream(path));
    std::vector<uint32_t> bytes; // no interesting data are wider than 32 bits (for now)

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

/** @brief Reads a int64_t number from a file. */
int64_t readFileInt64(const std::filesystem::path& path)
{
    std::ifstream ifs(openStream(path));
    int64_t res;

    if (ifs >> res) {
        return res;
    }

    throw std::domain_error("Could not read int64_t value from '" + std::string(path) + "'.");
}

/** @brief Reads whole contents of `path`. Throws if file doesn't exist. */
std::string readFileToString(const std::filesystem::path& path)
{
    std::ifstream ifs(openStream(path));

    std::istreambuf_iterator<char> begin(ifs), end;
    return std::string(begin, end);
}

void safeWriteFile(const std::string& filename, const std::string_view& contents)
{
    auto throwErr = [&filename] (const auto& what) {
        throw std::runtime_error(fmt::format("Couldn't write file '{}' ({}) ({})", filename, what, std::strerror(errno)));
    };
    // FIXME: not sure if just the tilde is fine...
    auto tempFileName = (filename + "~");
    auto f = std::fopen(tempFileName.c_str(), "w");
    if (!f) {
        throwErr("fopen");
    }
    if (std::fwrite(contents.data(), contents.size(), 1, f) != 1) {
        throwErr("fwrite");
    }
    if (fsync(fileno(f)) == -1) {
        throwErr("fsync");
    }
    if (std::fclose(f) == -1) {
        throwErr("fclose");
    }
    // std::rename is guaranteed to be atomic if replacing an old file
    if (std::rename(tempFileName.c_str(), filename.c_str()) == -1 ) {
        throwErr("std::rename");
    }
}

}
