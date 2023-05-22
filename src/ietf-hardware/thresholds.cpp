/*
 * Copyright (C) 2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <ostream>
#include <stdexcept>
#include "ietf-hardware/thresholds_fwd.h"

namespace velia::ietf_hardware {

std::ostream& operator<<(std::ostream& os, const State state)
{
    switch (state) {
    case State::Initial:
        os << "Unknown";
        break;
    case State::Disabled:
        os << "Disabled";
        break;
    case State::NoValue:
        os << "NoValue";
        break;
    case State::CriticalLow:
        os << "CriticalLow";
        break;
    case State::WarningLow:
        os << "WarningLow";
        break;
    case State::Normal:
        os << "Normal";
        break;
    case State::WarningHigh:
        os << "WarningHigh";
        break;
    case State::CriticalHigh:
        os << "CriticalHigh";
        break;
    }
    return os;
}
}
