/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 *
*/

#pragma once

#include <string>
#include <vector>

namespace velia::utils {

bool endsWith(const std::string& str, const std::string& suffix);
bool startsWith(const std::string& str, const std::string& prefix);
}
