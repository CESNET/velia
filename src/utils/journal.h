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

namespace velia::utils {
bool isJournaldActive();
std::shared_ptr<spdlog::sinks::sink> create_journald_sink();
}
