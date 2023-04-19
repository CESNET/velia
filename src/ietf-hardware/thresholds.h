/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#pragma once

#include <boost/signals2/signal.hpp>
#include <functional>
#include <optional>
#include "ietf-hardware/thresholds_fwd.h"

namespace velia::ietf_hardware {

template <typename Value>
struct OneThreshold {
    Value value;
    Value hysteresis;
};

template <typename Value>
struct Thresholds {
    std::optional<OneThreshold<Value>> criticalLow, warningLow, warningHigh, criticalHigh;
};

template <typename Value>
class Watcher {
public:
    using Signal = boost::signals2::signal<void(State)>;
    Signal changed;

    Watcher(const Thresholds<Value>& thresholds = Thresholds<Value>())
        : m_thresholds(thresholds)
    {
    }

    ~Watcher() = default;

    void setThresholds(const Thresholds<Value>& thresholds)
    {
        m_thresholds = thresholds;
        m_state = State::Initial;
        update(m_lastValue);
    }

    void update(const Value value)
    {
        if (violates<std::less>(value, m_thresholds.criticalLow)) {
            maybeTransition(State::CriticalLow, value);
        } else if (violates<std::greater>(value, m_thresholds.criticalHigh)) {
            maybeTransition(State::CriticalHigh, value);
        } else if (violates<std::less>(value, m_thresholds.warningLow)) {
            maybeTransition(State::WarningLow, value);
        } else if (violates<std::greater>(value, m_thresholds.warningHigh)) {
            maybeTransition(State::WarningHigh, value);
        } else if (!m_thresholds.criticalHigh && !m_thresholds.criticalLow && !m_thresholds.warningHigh && !m_thresholds.warningLow) {
            maybeTransition(State::Disabled, value);
        } else {
            maybeTransition(State::Normal, value);
        }
        m_lastValue = value;
    }

private:
    static bool isWithinHysteresis(const Value value, const std::optional<OneThreshold<Value>> threshold)
    {
        if (!threshold) {
            return false;
        }
        return value >= threshold->value - threshold->hysteresis && value <= threshold->value + threshold->hysteresis;
    }

    void maybeTransition(const State newState, const Value value)
    {
        if (newState != m_state) {
            changed(newState);
            m_state = newState;
            m_lastChange = value;
        }
    }

    template <template <typename> class Compare>
    bool violates(const Value value, const std::optional<OneThreshold<Value>> threshold)
    {
        if (!threshold) {
            return false;
        }

        const auto validHistory = m_state >= State::CriticalLow && m_state <= State::CriticalHigh;
        const auto beforeFuzzy = isWithinHysteresis(m_lastChange, threshold);
        const auto nowFuzzy = isWithinHysteresis(value, threshold);
        const auto before = Compare<Value>{}(m_lastChange, threshold->value);
        const auto now = Compare<Value>{}(value, threshold->value);

        if (now) {
            if (validHistory && !before && beforeFuzzy && nowFuzzy) {
                return false;
            } else {
                return true;
            }
        } else {
            if (validHistory && before && beforeFuzzy && nowFuzzy) {
                return true;
            } else {
                return false;
            }
        }
    }

    Thresholds<Value> m_thresholds;
    Value m_lastChange{std::numeric_limits<Value>::lowest()};
    Value m_lastValue{std::numeric_limits<Value>::lowest()};
    State m_state{State::Initial};
};

}
