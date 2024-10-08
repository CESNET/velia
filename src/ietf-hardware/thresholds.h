/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#pragma once

#include <functional>
#include <limits>
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
struct ThresholdUpdate {
    State newState;
    std::optional<Value> value;
    std::optional<Value> exceededThresholdValue;

    bool operator==(const ThresholdUpdate<Value>& other) const = default;
};

template <typename Value>
class Watcher {
public:
    Watcher(const Thresholds<Value>& thresholds = Thresholds<Value>())
        : m_thresholds(thresholds)
    {
    }

    ~Watcher() = default;

    std::optional<ThresholdUpdate<Value>> setThresholds(const Thresholds<Value>& thresholds)
    {
        m_thresholds = thresholds;
        m_state = State::NoValue;
        return update(m_lastValue);
    }

    std::optional<ThresholdUpdate<Value>> update(const std::optional<Value> value)
    {
        State oldState = m_state;
        std::optional<OneThreshold<Value>> violatedThresholdValue;

        if (!value) {
            maybeTransition(State::NoValue, value);
        } else if (violates<std::less>(value, m_thresholds.criticalLow)) {
            maybeTransition(State::CriticalLow, value);
            violatedThresholdValue = *m_thresholds.criticalLow;
        } else if (violates<std::greater>(value, m_thresholds.criticalHigh)) {
            maybeTransition(State::CriticalHigh, value);
            violatedThresholdValue = *m_thresholds.criticalHigh;
        } else if (violates<std::less>(value, m_thresholds.warningLow)) {
            maybeTransition(State::WarningLow, value);
            violatedThresholdValue = *m_thresholds.warningLow;
        } else if (violates<std::greater>(value, m_thresholds.warningHigh)) {
            maybeTransition(State::WarningHigh, value);
            violatedThresholdValue = *m_thresholds.warningHigh;
        } else if (!m_thresholds.criticalHigh && !m_thresholds.criticalLow && !m_thresholds.warningHigh && !m_thresholds.warningLow) {
            maybeTransition(State::Disabled, value);
        } else {
            maybeTransition(State::Normal, value);
        }

        m_lastValue = value;

        if (oldState != m_state) {
            if (violatedThresholdValue) {
                return ThresholdUpdate<Value>{m_state, value, violatedThresholdValue->value};
            } else {
                return ThresholdUpdate<Value>{m_state, value, std::nullopt};
            }
        }
        return std::nullopt;
    }

private:
    static bool isWithinHysteresis(const std::optional<Value>& value, const std::optional<OneThreshold<Value>> threshold)
    {
        if (!threshold || !value) {
            return false;
        }
        return value >= threshold->value - threshold->hysteresis && value <= threshold->value + threshold->hysteresis;
    }

    void maybeTransition(const State newState, const std::optional<Value>& value)
    {
        if (newState != m_state) {
            m_state = newState;
            m_lastChange = value;
        }
    }

    template <template <typename> class Compare>
    bool violates(const std::optional<Value>& value, const std::optional<OneThreshold<Value>> threshold)
    {
        if (!threshold || !value) {
            return false;
        }

        const auto validHistory = m_state >= State::CriticalLow && m_state <= State::CriticalHigh;
        const auto beforeFuzzy = isWithinHysteresis(m_lastChange, threshold);
        const auto nowFuzzy = isWithinHysteresis(value, threshold);
        const auto before = m_lastChange && Compare<Value>{}(*m_lastChange, threshold->value);
        const auto now = Compare<Value>{}(*value, threshold->value);

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
    std::optional<Value> m_lastChange{std::nullopt};
    std::optional<Value> m_lastValue{std::nullopt};
    State m_state{State::NoValue};
};

}
