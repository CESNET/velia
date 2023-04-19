/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <doctest/doctest.h>
#include <map>
#include <sstream>
#include <trompeloeil.hpp>

#include "ietf-hardware/IETFHardware.h"

namespace doctest {

template <>
struct StringMaker<std::map<std::string, std::string>> {
    static String convert(const std::map<std::string, std::string>& map)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << key << "\": \"" << value << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <>
struct StringMaker<std::map<std::string, int64_t>> {
    static String convert(const std::map<std::string, int64_t>& map)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << key << "\": " << value << "," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <class T>
struct StringMaker<std::vector<T>> {
    static String convert(const std::vector<T>& vec)
    {
        std::ostringstream os;
        os << "[";
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it != vec.begin()) {
                os << ", ";
            }
            os << StringMaker<T>::convert(*it);
        }
        os << "]";
        return os.str().c_str();
    }
};

template <>
struct StringMaker<velia::ietf_hardware::Alarm> {
    static String convert(const velia::ietf_hardware::Alarm& alarm)
    {
        std::ostringstream os;
        os << "(Alarm "
           << '"' << alarm.alarmType << ':' << alarm.alarmQualifier << "\" "
           << alarm.resource << " "
           << alarm.severity << ")";
        return os.str().c_str();
    }
};

}
