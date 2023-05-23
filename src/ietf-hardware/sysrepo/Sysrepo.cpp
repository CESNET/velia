/*
 * Copyright (C) 2020-2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <regex>
#include <sysrepo-cpp/Connection.hpp>
#include "Sysrepo.h"
#include "utils/alarms.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace {

const auto ALARM_CLEARED = "cleared";
const auto ALARM_SENSOR_MISSING = "velia-alarms:sensor-missing-alarm";
const auto ALARM_MISSING = "velia-alarms:sensor-missing-alarm";
const auto ALARM_MISSING_SEVERITY = "warning";
const auto ALARM_MISSING_DESCRIPTION = "Sensor value not reported. Maybe the sensor was unplugged?";
const auto ALARM_THRESHOLD_CROSSING_LOW = "velia-alarms:sensor-low-value-alarm";
const auto ALARM_THRESHOLD_CROSSING_LOW_DESCRIPTION = "Sensor value crossed low threshold.";
const auto ALARM_THRESHOLD_CROSSING_HIGH = "velia-alarms:sensor-high-value-alarm";
const auto ALARM_THRESHOLD_CROSSING_HIGH_DESCRIPTION = "Sensor value crossed high threshold.";

/** @brief Extracts component path prefix from an XPath under /ietf-hardware/component node
 *
 * Example input:  /ietf-hardware:hardware/component[name='ne:psu:child']/oper-state/disabled
 * Example output: /ietf-hardware:hardware/component[name='ne:psu:child']
 */
std::string extractComponentPrefix(const std::string& componentXPath)
{
    static const std::regex regex(R"((/ietf-hardware:hardware/component\[name=('|").*?(\2)\]).*)");
    std::smatch match;

    if (std::regex_match(componentXPath, match, regex)) {
        return match.str(1);
    }

    throw std::logic_error("Invalid xPath provided ('" + componentXPath + "')");
}

void logAlarm(velia::Log logger, const std::string_view sensor, const std::string_view alarm, const std::string_view severity, const velia::ietf_hardware::State state, const velia::ietf_hardware::State prevState)
{
    logger->trace("Sensor '{}': threshold state changed from '{}' to '{}'. Updating {} alarm to severity {}.", sensor, prevState, state, alarm, severity);
}

bool isThresholdCrossingLow(velia::ietf_hardware::State state)
{
    return state == velia::ietf_hardware::State::WarningLow || state == velia::ietf_hardware::State::CriticalLow;
}

bool isThresholdCrossingHigh(velia::ietf_hardware::State state)
{
    return state == velia::ietf_hardware::State::WarningHigh || state == velia::ietf_hardware::State::CriticalHigh;
}

std::string stateToSeverity(velia::ietf_hardware::State state)
{
    switch (state) {
    case velia::ietf_hardware::State::WarningLow:
    case velia::ietf_hardware::State::WarningHigh:
        return "warning";
    case velia::ietf_hardware::State::CriticalLow:
    case velia::ietf_hardware::State::CriticalHigh:
        return "critical";
    default: {
        std::ostringstream oss;
        oss << "No severity associated with sensor threshold State " << state;
        throw std::logic_error(oss.str());
    }
    }
}
}

namespace velia::ietf_hardware::sysrepo {

/** @brief The constructor expects the HardwareState instance which will provide the actual hardware state data and the poll interval */
Sysrepo::Sysrepo(::sysrepo::Session session, std::shared_ptr<IETFHardware> hwState, std::chrono::microseconds pollInterval)
    : m_log(spdlog::get("hardware"))
    , m_pollInterval(std::move(pollInterval))
    , m_session(std::move(session))
    , m_hwState(std::move(hwState))
    , m_quit(false)
{
    for (const auto& sensorXPath : m_hwState->sensorsXPaths()) {
        auto componentXPath = extractComponentPrefix(sensorXPath);
        utils::addResourceToAlarmInventoryEntry(m_session, ALARM_THRESHOLD_CROSSING_LOW, std::nullopt, componentXPath);
        utils::addResourceToAlarmInventoryEntry(m_session, ALARM_THRESHOLD_CROSSING_HIGH, std::nullopt, componentXPath);
        utils::addResourceToAlarmInventoryEntry(m_session, ALARM_SENSOR_MISSING, std::nullopt, componentXPath);
    }

    m_pollThread = std::thread([&]() {
        auto conn = m_session.getConnection();

        DataTree prevValues;
        std::map<std::string, State> thresholdsStates;

        while (!m_quit) {
            m_log->trace("IetfHardware poll");

            auto [hwStateValues, thresholds] = m_hwState->process();
            std::set<std::string> deletedComponents;

            /* Some data readers can stop returning data in some cases (e.g. ejected PSU).
             * Prune tree components that were removed before updating to avoid having not current data from previous invocations.
             */
            for (const auto& [k, v] : prevValues) {
                if (!hwStateValues.contains(k)) {
                    deletedComponents.emplace(extractComponentPrefix(k));
                }
            }

            std::vector<std::string> discards;
            discards.reserve(deletedComponents.size());
            std::copy(deletedComponents.begin(), deletedComponents.end(), std::back_inserter(discards));

            utils::valuesPush(hwStateValues, {}, discards, m_session, ::sysrepo::Datastore::Operational);

            for (const auto& [sensorXPath, state] : thresholds) {
                auto prevState = thresholdsStates.find(sensorXPath);

                if (state == State::NoValue) {
                    logAlarm(m_log, sensorXPath, ALARM_MISSING, ALARM_MISSING_SEVERITY, state, prevState == thresholdsStates.end() ? State::NoValue : prevState->second);
                    utils::createOrUpdateAlarm(m_session, ALARM_MISSING, std::nullopt, extractComponentPrefix(sensorXPath), ALARM_MISSING_SEVERITY, ALARM_MISSING_DESCRIPTION);
                } else if (state != State::NoValue && prevState != thresholdsStates.end() && prevState->second == State::NoValue) {
                    logAlarm(m_log, sensorXPath, ALARM_MISSING, ALARM_CLEARED, state, prevState == thresholdsStates.end() ? State::NoValue : prevState->second);
                    /* The alarm message is same for both setting and clearing the alarm. RFC8632 says that it is
                     * "The string used to inform operators about the alarm. This MUST contain enough information for an operator to be able to understand the problem and how to resolve it.",
                     * i.e., from my POV it does not make sense to say something like "cleared" when clearing the alarm as this would not be beneficial for the operator to understand what happened.
                     */
                    utils::createOrUpdateAlarm(m_session, ALARM_MISSING, std::nullopt, extractComponentPrefix(sensorXPath), ALARM_CLEARED, ALARM_MISSING_DESCRIPTION);
                }

                // set new threshold alarms first (in case the sensor value transitions from high to low (or low to high) so we don't lose any active alarm on the resource)
                if (isThresholdCrossingLow(state) && prevState != thresholdsStates.end() && prevState->second != state) {
                    logAlarm(m_log, sensorXPath, ALARM_THRESHOLD_CROSSING_LOW, stateToSeverity(state), state, prevState == thresholdsStates.end() ? State::NoValue : prevState->second);
                    utils::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_LOW, std::nullopt, extractComponentPrefix(sensorXPath), stateToSeverity(state), ALARM_THRESHOLD_CROSSING_LOW_DESCRIPTION);
                } else if (isThresholdCrossingHigh(state) && prevState != thresholdsStates.end() && prevState->second != state) {
                    logAlarm(m_log, sensorXPath, ALARM_THRESHOLD_CROSSING_HIGH, stateToSeverity(state), state, prevState == thresholdsStates.end() ? State::NoValue : prevState->second);
                    utils::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_HIGH, std::nullopt, extractComponentPrefix(sensorXPath), stateToSeverity(state), ALARM_THRESHOLD_CROSSING_HIGH_DESCRIPTION);
                }

                // clear old threshold alarms
                if (!isThresholdCrossingLow(state) && prevState != thresholdsStates.end() && isThresholdCrossingLow(prevState->second)) {
                    logAlarm(m_log, sensorXPath, ALARM_THRESHOLD_CROSSING_LOW, ALARM_CLEARED, state, prevState == thresholdsStates.end() ? State::NoValue : prevState->second);
                    utils::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_LOW, std::nullopt, extractComponentPrefix(sensorXPath), ALARM_CLEARED, ALARM_THRESHOLD_CROSSING_LOW_DESCRIPTION);
                } else if (!isThresholdCrossingHigh(state) && prevState != thresholdsStates.end() && isThresholdCrossingHigh(prevState->second)) {
                    logAlarm(m_log, sensorXPath, ALARM_THRESHOLD_CROSSING_HIGH, ALARM_CLEARED, state, prevState == thresholdsStates.end() ? State::NoValue : prevState->second);
                    utils::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_HIGH, std::nullopt, extractComponentPrefix(sensorXPath), ALARM_CLEARED, ALARM_THRESHOLD_CROSSING_HIGH_DESCRIPTION);
                }

                thresholdsStates[sensorXPath] = state;
            }

            prevValues = std::move(hwStateValues);
            std::this_thread::sleep_for(m_pollInterval);
        }
    });
}

Sysrepo::~Sysrepo()
{
    m_log->trace("Requesting poll thread stop");
    m_quit = true;
    m_pollThread.join();
}
}
