/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <doctest/doctest.h>
#include <experimental/iterator>
#include <map>
#include <set>
#include <sstream>
#include <trompeloeil.hpp>
#include "ietf-hardware/sysfs/IpmiFruEEPROM.h"
#include "ietf-hardware/sysfs/OnieEEPROM.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/thresholds.h"
#include "network/IETFInterfacesConfig.h"
#include "network/NetworkctlUtils.h"
#include "tests/sysrepo-helpers/common.h"

namespace doctest {

template <>
struct StringMaker<int64_t> {
    static String convert(const int64_t v)
    {
        return std::to_string(v).c_str();
    }
};

template <class T>
struct StringMaker<std::vector<T>> {
    static String convert(const std::vector<T>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& value : v) {
            if constexpr (std::is_same_v<T, uint8_t>) {
                os << fmt::format("  {:#04x}\n", value);
            } else {
                os << "  \"" << StringMaker<T>::convert(value) << "\"," << std::endl;
            }
        }
        os << "}";
        return os.str().c_str();
    }
};

template <class T>
struct StringMaker<std::set<T>> {
    static String convert(const std::set<T>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& value : v) {
            os << "  \"" << StringMaker<T>::convert(value) << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <class T, class U>
struct StringMaker<std::map<T, U>> {
    static String convert(const std::map<T, U>& map)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << StringMaker<T>::convert(key) << "\": " << StringMaker<U>::convert(value) << "," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <class T>
struct StringMaker<velia::ietf_hardware::ThresholdUpdate<T>> {
    static String convert(const velia::ietf_hardware::ThresholdUpdate<T>& x) {
        std::ostringstream oss;
        oss << "ThresholdUpdate{new state: " << x.newState;
        if (x.value) {
            oss << ", new value: " << StringMaker<T>::convert(*x.value);
        }
        if (x.exceededThresholdValue) {
            oss << " exceeds threshold: " << StringMaker<T>::convert(*x.exceededThresholdValue);
        }
        oss << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<velia::ietf_hardware::SideLoadedAlarm> {
    static String convert(const velia::ietf_hardware::SideLoadedAlarm& a) {
        std::ostringstream oss;
        oss << "SideLoadedAlarm{"
            << a.alarmTypeId << ", "
            << a.resource << ", "
            << a.severity << ", "
            << a.text << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<velia::ietf_hardware::sysfs::ProductInfo> {
    static String convert(const velia::ietf_hardware::sysfs::ProductInfo& e)
    {
        std::ostringstream oss;
        oss << "ProductInfo{manufacturer: >" << e.manufacturer << "<, "
            << "name: >" << e.name << "<, "
            << "partNumber: >" << e.partNumber << "<, "
            << "version: >" << e.version << "<, "
            << "serialNumber: >" << e.serialNumber << "<, "
            << "assetTag: >" << e.assetTag << "<, "
            << "fruFileId: >" << e.fruFileId << "<, "
            << "custom: " << StringMaker<decltype(e.custom)>::convert(e.custom) << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<velia::ietf_hardware::sysfs::CommonHeader> {
    static String convert(const velia::ietf_hardware::sysfs::CommonHeader& e)
    {
        std::ostringstream oss;
        oss << "CommonHeader{internalUseAreaOfs: " << static_cast<int>(e.internalUseAreaOfs) << ", "
            << "chassisInfoAreaOfs: " << static_cast<int>(e.chassisInfoAreaOfs) << ", "
            << "boardAreaOfs: " << static_cast<int>(e.boardAreaOfs) << ", "
            << "productInfoAreaOfs: " << static_cast<int>(e.productInfoAreaOfs) << ", "
            << "multiRecordAreaOfs: " << static_cast<int>(e.multiRecordAreaOfs) << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<velia::ietf_hardware::sysfs::FRUInformationStorage> {
    static String convert(const velia::ietf_hardware::sysfs::FRUInformationStorage& e)
    {
        return (std::string{"EEPROM{"} + StringMaker<decltype(e.header)>::convert(e.header).c_str() + ", " + StringMaker<decltype(e.productInfo)>::convert(e.productInfo).c_str() + "}").c_str();
    }
};

template <>
struct StringMaker<velia::ietf_hardware::sysfs::TLV> {
    static String convert(const velia::ietf_hardware::sysfs::TLV& e)
    {
        constexpr auto visitor = [](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::string>) {
                return arg;
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                return fmt::format(" {:02x}", arg);
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                std::ostringstream oss;
                for (const auto& e : arg) {
                    oss << fmt::format(" {:02x}", e);
                }
                return oss.str();
            } else {
                return "unknown";
            }
        };

        return fmt::format("TLV{{type: {}, value: {}}}", static_cast<int>(e.type), std::visit<std::string>(visitor, e.value)).c_str();
    }
};

template <>
struct StringMaker<velia::network::NetworkConfFiles> {
    static String convert(const velia::network::NetworkConfFiles& x)
    {
        std::ostringstream oss;
        oss << "NetworkFiles{.networkFile = " << x.networkFile.value_or("n/a") << ", .dropinFiles = {";
        std::copy(std::begin(x.dropinFiles), std::end(x.dropinFiles), std::experimental::make_ostream_joiner(oss, ", "));
        oss << "}}";
        return oss.str().c_str();
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

template <>
struct printer<velia::network::IETFInterfacesConfig::ChangedUnits> {
    static void print(std::ostream& os, const velia::network::IETFInterfacesConfig::ChangedUnits& x)
    {
        os << "ChangedUnits{.deleted = {";
        std::copy(std::begin(x.deleted), std::end(x.deleted), std::experimental::make_ostream_joiner(os, ", "));
        os << "}, .changedOrNew = {";
        std::copy(std::begin(x.changedOrNew), std::end(x.changedOrNew), std::experimental::make_ostream_joiner(os, ", "));
        os << "}}";
    }
};
}
