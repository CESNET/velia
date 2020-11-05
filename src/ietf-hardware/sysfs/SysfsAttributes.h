/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#pragma once

#include <map>
#include <string>

namespace velia::ietf_hardware::sysfs {

using EMMCAttributes = std::map<std::string, std::string>;
}
