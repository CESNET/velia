/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "sysfs/Exceptions.h"

namespace velia::hardware::sysfs {

Error::Error(const std::string& what)
    : std::runtime_error(what)
{
}

FileDoesNotExist::FileDoesNotExist(const std::string& what)
    : Error(what)
{
}

ParseError::ParseError(const std::string& what)
    : Error(what)
{
}

}
