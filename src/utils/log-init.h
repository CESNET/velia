/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <memory>

namespace spdlog {
namespace sinks {
class sink;
}
}

/** @file
  * @short Implementation of initialization of logging
*/

namespace cla {
namespace utils {
void initLogs(std::shared_ptr<spdlog::sinks::sink> sink);
}
}
