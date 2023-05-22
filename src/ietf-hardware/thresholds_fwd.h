/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#pragma once
#include <fmt/ostream.h>
#include <iosfwd>

namespace velia::ietf_hardware {

template <typename Value>
struct Thresholds;

template <typename Value>
class Watcher;

enum class State {
    NoValue, /**< @short No value associated (after initialization, or after updating with empty value). */
    Disabled, /**< @short No thresholds are set. */
    CriticalLow,
    WarningLow,
    Normal,
    WarningHigh,
    CriticalHigh,
};

std::ostream& operator<<(std::ostream& os, const State state);
}
