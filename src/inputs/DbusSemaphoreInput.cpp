/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#include "DbusSemaphoreInput.h"
#include "utils/log.h"

namespace {

velia::State stateFromString(const std::string& str)
{
    if (str == "WARNING")
        return velia::State::WARNING;
    if (str == "ERROR")
        return velia::State::ERROR;
    if (str == "OK")
        return velia::State::OK;

    throw std::invalid_argument("DbusSemaphoreInput received invalid state");
}
}

namespace velia {

DbusSemaphoreInput::DbusSemaphoreInput(std::shared_ptr<AbstractManager> manager, sdbus::IConnection& connection, const std::string& bus, const std::string& objectPath, const std::string& propertyName, const std::string& propertyInterface)
    : AbstractInput(std::move(manager))
    , m_dbusObjectProxy(sdbus::createProxy(connection, bus, objectPath))
    , m_propertyName(propertyName)
    , m_propertyInterface(propertyInterface)
    , m_log(spdlog::get("main"))
{
    m_dbusObjectProxy->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call([&](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, [[maybe_unused]] const std::vector<std::string>& invalidated) {
        if (iface != m_propertyInterface) {
            return;
        }

        if (auto it = changed.find(m_propertyName); it != changed.end()) {
            std::string newState = it->second.get<std::string>();
            m_log->trace("Property changed to {}", newState);
            updateState(stateFromString(newState));
        }
    });
    m_dbusObjectProxy->finishRegistration();
    m_log->trace("Watching for property changes of {}, object {}, property {}.{}", bus, objectPath, propertyInterface, propertyName);

    // we might update the state twice here (once from the callback, once from here).
    // But better than querying the current state before the registration; we might miss a state change that could happen between querying and callback registration
    std::string currentState = m_dbusObjectProxy->getProperty(m_propertyName).onInterface(propertyInterface).get<std::string>();
    m_log->trace("Property initialized to {}", currentState);
    updateState(stateFromString(currentState));
}

DbusSemaphoreInput::~DbusSemaphoreInput() = default;

}