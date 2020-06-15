/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <memory>

/** @file
  * @short Forward-declarations for use of utils/log.h, i.e. for log producers
*/

namespace spdlog {
class logger;
}

namespace velia {
using Log = std::shared_ptr<spdlog::logger>;
}
