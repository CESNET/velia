/*
 * Copyright (C) 2020-2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <boost/algorithm/string.hpp>
#include <regex>
#include <sysrepo-cpp/Connection.hpp>
#include "Sysrepo.h"
#include "utils/alarms.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace {

const auto ALARM_CLEARED = "cleared";
const auto ALARM_SENSOR_MISSING = "velia-alarms:sensor-missing-alarm";
const auto ALARM_MISSING_SEVERITY = "warning";
const auto ALARM_MISSING_DESCRIPTION = "Sensor value not reported. Maybe the sensor was unplugged?";
const auto ALARM_THRESHOLD_CROSSING_LOW = "velia-alarms:sensor-low-value-alarm";
const auto ALARM_THRESHOLD_CROSSING_LOW_DESCRIPTION = "Sensor value crossed low threshold.";
const auto ALARM_THRESHOLD_CROSSING_HIGH = "velia-alarms:sensor-high-value-alarm";
const auto ALARM_THRESHOLD_CROSSING_HIGH_DESCRIPTION = "Sensor value crossed high threshold.";
const auto ALARM_SENSOR_NONOPERATIONAL = "velia-alarms:sensor-nonoperational";
const auto ALARM_SENSOR_NONOPERATIONAL_SEVERITY = "warning";
const auto ALARM_SENSOR_NONOPERATIONAL_DESCRIPTION = "Sensor is nonoperational. The values it reports may not be relevant.";

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

void logAlarm(velia::Log logger, const std::string_view sensor, const std::string_view alarm, const std::string_view severity)
{
    logger->info("Alarm {}: {} for {}", alarm, severity, sensor);
}

bool isThresholdCrossingLow(velia::ietf_hardware::State state)
{
    return state == velia::ietf_hardware::State::WarningLow || state == velia::ietf_hardware::State::CriticalLow;
}

bool isThresholdCrossingHigh(velia::ietf_hardware::State state)
{
    return state == velia::ietf_hardware::State::WarningHigh || state == velia::ietf_hardware::State::CriticalHigh;
}

std::string toYangAlarmSeverity(velia::ietf_hardware::State state)
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
    m_pollThread = std::thread([&]() {
        auto conn = m_session.getConnection();

        DataTree prevValues;
        std::set<std::string> seenSensors;
        std::map<std::string, State> thresholdsStates;
        std::set<std::pair<std::string, std::string>> activeSideLoadedAlarms;

        while (!m_quit) {
            m_log->trace("IetfHardware poll");

            auto [hwStateValues, thresholds, activeSensors, sideLoadedAlarms] = m_hwState->process();
            std::set<std::string> deletedComponents;

            for (const auto& sensorXPath : activeSensors) {
                if (!seenSensors.contains(sensorXPath)) {
                    auto componentXPath = extractComponentPrefix(sensorXPath);
                    utils::alarms::addResourceToAlarmInventoryEntry(m_session, ALARM_THRESHOLD_CROSSING_LOW, std::nullopt, componentXPath);
                    utils::alarms::addResourceToAlarmInventoryEntry(m_session, ALARM_THRESHOLD_CROSSING_HIGH, std::nullopt, componentXPath);
                    utils::alarms::addResourceToAlarmInventoryEntry(m_session, ALARM_SENSOR_MISSING, std::nullopt, componentXPath);
                    utils::alarms::addResourceToAlarmInventoryEntry(m_session, ALARM_SENSOR_NONOPERATIONAL, std::nullopt, componentXPath);
                }
            }
            seenSensors.merge(activeSensors);

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

            /* Publish sideloaded alarms */
            for (const auto& [alarm, resource, severity, text] : sideLoadedAlarms) {
                // Sideloaded alarms are not registered using the code above, let's register those too
                utils::alarms::addResourceToAlarmInventoryEntry(m_session, ALARM_SENSOR_MISSING, std::nullopt, resource);

                bool isActive = activeSideLoadedAlarms.contains({alarm, resource});
                if (isActive && severity == "cleared") {
                    utils::alarms::createOrUpdateAlarm(m_session, alarm, std::nullopt, resource, "cleared", text);
                    activeSideLoadedAlarms.erase({alarm, resource});
                } else if (!isActive && severity != "cleared") {
                    utils::alarms::createOrUpdateAlarm(m_session, alarm, std::nullopt, resource, severity, text);
                    activeSideLoadedAlarms.insert({alarm, resource});
                }
            }

            /* Look for nonoperational sensors to set alarms */
            for (const auto& [leaf, value] : hwStateValues) {
                if (boost::ends_with(leaf, "/sensor-data/oper-status")) {
                    std::optional<std::string> oldValue;

                    if (auto it = prevValues.find(leaf); it != prevValues.end()) {
                        oldValue = it->second;
                    }

                    if (value == "nonoperational" && oldValue != "nonoperational") {
                        utils::alarms::createOrUpdateAlarm(m_session, ALARM_SENSOR_NONOPERATIONAL, std::nullopt, extractComponentPrefix(leaf), ALARM_SENSOR_NONOPERATIONAL_SEVERITY, ALARM_SENSOR_NONOPERATIONAL_DESCRIPTION);
                    } else if (value == "ok" && oldValue && oldValue != "ok" /* don't call clear-alarm if we see this node for the first time, i.e., oldvalue is nullopt */) {
                        utils::alarms::createOrUpdateAlarm(m_session, ALARM_SENSOR_NONOPERATIONAL, std::nullopt, extractComponentPrefix(leaf), ALARM_CLEARED, ALARM_SENSOR_NONOPERATIONAL_DESCRIPTION);
                    }
                }
            }

            for (const auto& [sensorXPath, state] : thresholds) {
                // missing prevState can be considered as Normal
                const State prevState = [&, sensorXPath = sensorXPath] {
                    if (auto it = thresholdsStates.find(sensorXPath); it != thresholdsStates.end()) {
                        return it->second;
                    }
                    return State::Normal;
                }();
                const auto componentXPath = extractComponentPrefix(sensorXPath);

                if (state == State::NoValue) {
                    logAlarm(m_log, componentXPath, ALARM_SENSOR_MISSING, ALARM_MISSING_SEVERITY);
                    utils::alarms::createOrUpdateAlarm(m_session, ALARM_SENSOR_MISSING, std::nullopt, componentXPath, ALARM_MISSING_SEVERITY, ALARM_MISSING_DESCRIPTION);
                } else if (prevState == State::NoValue) {
                    logAlarm(m_log, componentXPath, ALARM_SENSOR_MISSING, ALARM_CLEARED);
                    /* The alarm message is same for both setting and clearing the alarm. RFC8632 says that it is
                     * "The string used to inform operators about the alarm. This MUST contain enough information for an operator to be able to understand the problem and how to resolve it.",
                     * i.e., from my POV it does not make sense to say something like "cleared" when clearing the alarm as this would not be beneficial for the operator to understand what happened.
                     */
                    utils::alarms::createOrUpdateAlarm(m_session, ALARM_SENSOR_MISSING, std::nullopt, componentXPath, ALARM_CLEARED, ALARM_MISSING_DESCRIPTION);
                }

                /*
                 * We set new threshold alarms first. In case the sensor value transitions from high to low (or low to high) we don't want to lose any active alarm on the resource.
                 *
                 * In case new state corresponds to threshold crossing (wither lower bound or upper bound) we set the alarm.
                 * Since we receive only changes to states it should be sufficient to just check if the new state crossed the threshold.
                 * We shouldn't receive any "no-op" state change (e.g. warning low to warning low) and even if we did receive such change, we would only set the same alarm again.
                 * We can however receive a change from critical threshold to warning threshold (or warning to critical) but that is no problem.
                 * We only need to set the same alarm again with the new severity.
                 */
                if (isThresholdCrossingLow(state)) {
                    logAlarm(m_log, componentXPath, ALARM_THRESHOLD_CROSSING_LOW, toYangAlarmSeverity(state));
                    utils::alarms::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_LOW, std::nullopt, componentXPath, toYangAlarmSeverity(state), ALARM_THRESHOLD_CROSSING_LOW_DESCRIPTION);
                } else if (isThresholdCrossingHigh(state)) {
                    logAlarm(m_log, componentXPath, ALARM_THRESHOLD_CROSSING_HIGH, toYangAlarmSeverity(state));
                    utils::alarms::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_HIGH, std::nullopt, componentXPath, toYangAlarmSeverity(state), ALARM_THRESHOLD_CROSSING_HIGH_DESCRIPTION);
                }

                /* Now we can clear the old threshold alarms that are no longer active, i.e., we transition away from the CriticalLow/WarningLow or CriticalHigh/WarningHigh. */
                if (!isThresholdCrossingLow(state) && isThresholdCrossingLow(prevState)) {
                    logAlarm(m_log, componentXPath, ALARM_THRESHOLD_CROSSING_LOW, ALARM_CLEARED);
                    utils::alarms::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_LOW, std::nullopt, componentXPath, ALARM_CLEARED, ALARM_THRESHOLD_CROSSING_LOW_DESCRIPTION);
                } else if (!isThresholdCrossingHigh(state) && isThresholdCrossingHigh(prevState)) {
                    logAlarm(m_log, componentXPath, ALARM_THRESHOLD_CROSSING_HIGH, ALARM_CLEARED);
                    utils::alarms::createOrUpdateAlarm(m_session, ALARM_THRESHOLD_CROSSING_HIGH, std::nullopt, componentXPath, ALARM_CLEARED, ALARM_THRESHOLD_CROSSING_HIGH_DESCRIPTION);
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
