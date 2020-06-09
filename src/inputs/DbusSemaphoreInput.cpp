#include "DbusSemaphoreInput.h"
#include "utils/log.h"

namespace {

State stateFromString(const std::string& str)
{
    if (str == "WARNING")
        return State::WARNING;
    if (str == "ERROR")
        return State::ERROR;
    if (str == "OK")
        return State::OK;

    throw std::invalid_argument("DbusSemaphoreInput received invalid state");
}
}

namespace cla {

DbusSemaphoreInput::DbusSemaphoreInput(std::shared_ptr<AbstractManager> manager, sdbus::IConnection& connection, const std::string& bus, const std::string& objectPath, const std::string& propertyName, const std::string propertyInterface)
    : AbstractInput(manager)
    , m_dbusObjectProxy(sdbus::createProxy(connection, bus, objectPath))
    , m_propertyName(propertyName)
    , m_propertyInterface(propertyInterface)
    , m_log(spdlog::get("input"))
{
    //sdbus::Variant xx = m_dbusObjectProxy->getProperty("Semaphore").onInterface("cz.cesnet.Led");

    m_dbusObjectProxy->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call([&](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, [[maybe_unused]] const std::vector<std::string>& invalidated) {
        std::map<std::string, sdbus::Variant>::const_iterator it;

        if (iface == m_propertyInterface && (it = changed.find(m_propertyName)) != changed.end()) {
            m_log->trace("DbusSemaphore: Property {}.{} changed: {}", m_propertyInterface, m_propertyName, std::string(it->second));
            updateState(stateFromString(it->second));
        }
    });
    m_dbusObjectProxy->finishRegistration();
    m_log->trace("DbusSemaphoreInput registered on bus {}, object {}, property {}.{}", bus, objectPath, propertyInterface, propertyName);
}

DbusSemaphoreInput::~DbusSemaphoreInput() = default;

}