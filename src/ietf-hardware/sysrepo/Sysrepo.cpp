/*
 * Copyright (C) 2020-2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <boost/algorithm/string/predicate.hpp>
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
    static const std::string prefix = "/ietf-hardware:hardware/component[name=";

    if (!boost::algorithm::starts_with(componentXPath, prefix)) {
        throw std::logic_error("Invalid XPath '"s + componentXPath + "'");
    }

    char quotes;
    if (!componentXPath.empty() && componentXPath[prefix.size()] == '\'') {
        quotes = '\'';
    } else if (!componentXPath.empty() && componentXPath[prefix.size()] == '\"') {
        quotes = '\"';
    } else {
        throw std::logic_error("Invalid XPath '"s + componentXPath + "'");
    }

    auto nextQuote = componentXPath.find_first_of(quotes, prefix.size() + 1);
    if (nextQuote == std::string::npos) {
        throw std::logic_error("Invalid XPath '"s + componentXPath + "'");
    }

    return componentXPath.substr(0, nextQuote + 2 /* quote itself, closing bracket */);
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
        m_log->trace("Poll thread started");
        auto conn = m_session.getConnection();

        DataTree prevValues;

        while (!m_quit) {
            m_log->trace("IetfHardware poll");

            auto hwStateValues = m_hwState->process();
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

            prevValues = std::move(hwStateValues);
            std::this_thread::sleep_for(m_pollInterval);
        }
        m_log->trace("Poll thread stopped");
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
