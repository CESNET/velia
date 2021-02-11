/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

/** @file
  * @short Include this to be able to produce logs
*/

//#define SPDLOG_ENABLE_SYSLOG
#include <spdlog/spdlog.h>

namespace docopt {
    class value;
}

/** @short Extract log level from a CLI option */
spdlog::level::level_enum parseLogLevel(const std::string& name, const docopt::value& option);
