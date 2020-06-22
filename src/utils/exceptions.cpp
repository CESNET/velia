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
    char* classname = __cxxabiv1::__cxa_demangle(typeid(e).name(), nullptr, 0, &demangled);
    log->critical("Fatal error in {}: {}", when, demangled == 0 ? classname : typeid(e).name());
    log->critical("{}", e.what());
    free(classname);
    throw;
}
}
