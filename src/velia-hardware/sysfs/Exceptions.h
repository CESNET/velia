/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <stdexcept>

namespace velia::hardware::sysfs {

class Error : public std::runtime_error {
public:
    Error(const std::string& what);
    ~Error() override = default;
};

class FileDoesNotExist : public Error {
public:
    FileDoesNotExist(const std::string& what);
    ~FileDoesNotExist() override = default;
};

class ParseError : public Error {
public:
    ParseError(const std::string& what);
    ~ParseError() override = default;
};

}
