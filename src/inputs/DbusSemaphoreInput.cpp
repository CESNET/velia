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

DbusSemaphoreInput::DbusSemaphoreInput(std::shared_ptr<AbstractManager> manager, sdbus::IConnection& connection, const std::string& bus, const std::string& objectPath, const std::string& propertyName, const std::string propertyInterface)
    : AbstractInput(manager)
    , m_dbusObjectProxy(sdbus::createProxy(connection, bus, objectPath))
    , m_propertyName(propertyName)
    , m_propertyInterface(propertyInterface)
    , m_log(spdlog::get("input"))
{
    m_dbusObjectProxy->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call([&](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, [[maybe_unused]] const std::vector<std::string>& invalidated) {
        std::map<std::string, sdbus::Variant>::const_iterator it;

        if (iface == m_propertyInterface && (it = changed.find(m_propertyName)) != changed.end()) {
            m_log->trace("DbusSemaphore: Property {}.{} changed: {}", m_propertyInterface, m_propertyName, std::string(it->second));
            updateState(stateFromString(it->second));
        }
    });
    m_dbusObjectProxy->finishRegistration();
    m_log->trace("DbusSemaphoreInput registered on bus {}, object {}, property {}.{}", bus, objectPath, propertyInterface, propertyName);

    // we might update the state twice here (once from the callback, once from here).
    // But better than querying the current state before the registration; we might miss a change that could happen between querying and callback registration

    m_log->trace("query");
    sdbus::Variant currentState = m_dbusObjectProxy->getProperty(m_propertyName).onInterface(propertyInterface);
    m_log->trace("CurrentState: {}", (std::string)currentState);
    updateState(stateFromString(currentState));
}

DbusSemaphoreInput::~DbusSemaphoreInput() = default;

}