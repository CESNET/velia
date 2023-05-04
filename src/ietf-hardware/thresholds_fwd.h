/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#pragma once
#include <ostream>

namespace velia::ietf_hardware {

template <typename Value>
struct Thresholds;

template <typename Value>
class Watcher;

enum class State {
    Initial, /**< @short Unknown state. This should never be visible externally. */
    Disabled, /**< @short No thresholds are set. */
    CriticalLow,
    WarningLow,
    Normal,
    WarningHigh,
    CriticalHigh,
};

std::ostream& operator<<(std::ostream& os, const State state);

}
