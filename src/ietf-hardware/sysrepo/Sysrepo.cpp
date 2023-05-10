/*
 * Copyright (C) 2020-2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <regex>
#include <sysrepo-cpp/Connection.hpp>
#include "Sysrepo.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace {

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
    , m_pollThread([&]() {
        auto conn = m_session.getConnection();

        DataTree prevValues;
        std::map<std::string, ThresholdInfo> prevThresholds;

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

            for (const auto& [sensorXPath, info] : thresholds) {
                auto itPrevInfo = prevThresholds.find(sensorXPath);

                if (info.disappeared == true && itPrevInfo->second.disappeared == false) {
                    // invoke alarm disappeared
                } else if (info.disappeared == false && itPrevInfo->second.disappeared == true) {
                    // clear alarm disappeared
                } else if (info.state) {
                    // create or update alarm thresholds based on the state
                    // State::Normal -> CLEAR
                    // State::WarningLow -> WARN
                    // State::WarningHigh -> WARN
                    // State::CriticalHigh -> CRIT
                    // State::CriticalLow -> CRIT
                }
            }

            utils::valuesPush(hwStateValues, {}, m_session, ::sysrepo::Datastore::Operational);

            prevValues = std::move(hwStateValues);
            prevThresholds = std::move(thresholds);
            std::this_thread::sleep_for(m_pollInterval);
        }
    })
{
}

Sysrepo::~Sysrepo()
{
    m_log->trace("Requesting poll thread stop");
    m_quit = true;
    m_pollThread.join();
}
}
