/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include <vector>
#include "utils/log-init.h"
#include "utils/log.h"

namespace cla {
namespace utils {

/** @short Initialize logging

Creates and registers all required loggers and connect them to the provided sink.
*/
void initLogs(std::shared_ptr<spdlog::sinks::sink> sink)
{
    for (const auto& name : std::vector<std::string>{"main"}) {
        spdlog::register_logger(std::make_shared<spdlog::logger>(name, sink));
    }
}
}
}
