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

const auto ALARM_THRESHOLD = "velia-alarms:threshold-crossing-alarm";
const auto ALARM_THRESHOLD_DESCRIPTION = "Sensor threshold crossed.";
const auto ALARM_MISSING = "velia-alarms:sensor-missing";
const auto ALARM_MISSING_DESCRIPTION = "Sensor is missing. Maybe it was unplugged?";

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
        utils::addResourceToAlarmInventoryEntry(m_session, ALARM_THRESHOLD, std::nullopt, componentXPath);
        utils::addResourceToAlarmInventoryEntry(m_session, ALARM_MISSING, std::nullopt, componentXPath);
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

            for (const auto& component : deletedComponents) {
                conn.discardOperationalChanges(component);
            }

            utils::valuesPush(hwStateValues, {}, m_session, ::sysrepo::Datastore::Operational);

            for (const auto& [sensorXPath, state] : thresholds) {
                auto prevState = thresholdsStates.find(sensorXPath);

                if (state == State::NoValue) {
                    utils::createOrUpdateAlarm(m_session, ALARM_MISSING, std::nullopt, extractComponentPrefix(sensorXPath), "critical", ALARM_MISSING_DESCRIPTION);
                } else if (prevState != thresholdsStates.end() && prevState->second == State::NoValue && state != State::NoValue) {
                    utils::createOrUpdateAlarm(m_session, ALARM_MISSING, std::nullopt, extractComponentPrefix(sensorXPath), "cleared", ALARM_MISSING_DESCRIPTION);
                }

                if (state == State::CriticalLow || state == State::CriticalHigh) {
                    utils::createOrUpdateAlarm(m_session, ALARM_THRESHOLD, std::nullopt, extractComponentPrefix(sensorXPath), "critical", ALARM_THRESHOLD_DESCRIPTION);
                } else if (state == State::WarningLow || state == State::WarningHigh) {
                    utils::createOrUpdateAlarm(m_session, ALARM_THRESHOLD, std::nullopt, extractComponentPrefix(sensorXPath), "warning", ALARM_THRESHOLD_DESCRIPTION);
                } else if (state == State::Normal && prevState != thresholdsStates.end() && prevState->second >= State::CriticalLow && prevState->second <= State::CriticalHigh) {
                    utils::createOrUpdateAlarm(m_session, ALARM_THRESHOLD, std::nullopt, extractComponentPrefix(sensorXPath), "cleared", ALARM_THRESHOLD_DESCRIPTION);
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
