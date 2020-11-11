/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include <boost/algorithm/string/predicate.hpp>
#include "utils/string.h"

namespace velia::utils {

/** @short Returns true if str ends with a given suffix */
bool endsWith(const std::string& str, const std::string& suffix)
{
    return boost::algorithm::ends_with(str, suffix);
}

/** @short Returns true if str starts with a given prefix */
bool startsWith(const std::string& str, const std::string& prefix)
{
    return boost::algorithm::starts_with(str, prefix);
}
}
