/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include <cxxabi.h>
#include "utils/exceptions.h"
#include "utils/log.h"

namespace velia::utils {
/** @short Log that everything is screwed up and rethrow

The purpose is to make sure that a nicely formatted error message gets stored into the journald buffer with a high enough priority.
*/
void fatalException [[noreturn]] (velia::Log log, const std::exception& e, const std::string& when)
{
    int demangled;
    auto classname =
        std::unique_ptr<char, decltype(&std::free)>(__cxxabiv1::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &demangled), std::free);
    log->critical("Fatal error in {}: {}", when, demangled == 0 ? classname.get() : typeid(e).name());
    log->critical("{}", e.what());
    throw;
}
}
