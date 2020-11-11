/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 *
*/

#include <algorithm>
#include "utils/string.h"

namespace velia::utils {

/** @short Returns true if str ends with a given suffix */
bool endsWith(const std::string& str, const std::string& suffix)
{
    if (suffix.size() > str.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

/** @short Returns true if str starts with a given prefix */
bool startsWith(const std::string& str, const std::string& prefix)
{
    if (prefix.size() > str.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), str.begin());
}
}
