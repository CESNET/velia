/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <doctest/doctest.h>
#include <map>
#include <set>
#include <sstream>
#include <trompeloeil.hpp>
#include "ietf-hardware/thresholds.h"
#include "tests/sysrepo-helpers/common.h"

namespace doctest {

template <class T>
struct StringMaker<std::vector<T>> {
    static String convert(const std::vector<T>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& value : v) {
            os << "  \"" << value << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <>
struct StringMaker<std::set<std::string>> {
    static String convert(const std::set<std::string>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& value : v) {
            os << "  \"" << value << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

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

template <>
struct StringMaker<std::map<std::string, velia::ietf_hardware::State>> {
    static String convert(const std::map<std::string, velia::ietf_hardware::State>& map)
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
}

namespace trompeloeil {
template <>
struct printer<ValueChanges> {
    static void print(std::ostream& os, const ValueChanges& map)
    {
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << key << "\": \""
               << std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, Deleted>)
                    return "Deleted()";
                if constexpr (std::is_same_v<T, std::string>)
                    return arg; }, value)
               << "\"," << std::endl;
        }
        os << "}";
    }
};
}
