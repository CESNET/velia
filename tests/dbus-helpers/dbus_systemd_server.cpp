#include <sdbus-c++/sdbus-c++.h>
#include "dbus_systemd_server.h"
#include "utils/log-init.h"

using namespace std::literals;

namespace {
static const std::string interfaceUnit = "org.freedesktop.systemd1.Unit";
static const std::string interfaceManager = "org.freedesktop.systemd1.Manager";
static const std::string objectPathManager = "/org/freedesktop/systemd1";
}

/** @brief Create a dbus server on the connection */
DbusSystemdServer::DbusSystemdServer(sdbus::IConnection& connection)
    : m_manager(sdbus::createObject(connection, objectPathManager))
{
    // create manager object
    m_manager->registerMethod("Subscribe").onInterface(interfaceManager).implementedAs([] {}).withNoReply(); // no-op for us
    m_manager->registerMethod("ListUnits").onInterface(interfaceManager).implementedAs([this]() { return ListUnits(); });
    m_manager->registerSignal("UnitNew").onInterface(interfaceManager).withParameters<std::string, sdbus::ObjectPath>();
    m_manager->finishRegistration();
}

/** @brief Creates a unit inside the test server. Registers dbus object and emits UnitNew signal. **/
void DbusSystemdServer::createUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState)
{
    m_units.insert(std::make_pair(objPath, Unit(unitName, sdbus::createObject(connection, objPath), activeState, subState)));

    Unit& unit = m_units.at(objPath);

    unit.m_object->registerProperty("ActiveState").onInterface(interfaceUnit).withGetter([&unit]() {
        return unit.m_activeState;
    });
    unit.m_object->registerProperty("SubState").onInterface(interfaceUnit).withGetter([&unit]() {
        return unit.m_subState;
    });
    unit.m_object->finishRegistration();

    m_manager->emitSignal("UnitNew").onInterface(interfaceManager).withArguments(unitName, objPath);
}

/**
 * @brief Implementation of ListUnit method.
 *
 * The unit is represented as a (objectPath, activeState, subState) triplet only. Nothing more is required now.
 * Therefore, the not interesting properties are unused.
 * In real systemd, there are more properties (see ListUnits method in https://www.freedesktop.org/wiki/Software/systemd/dbus/)
 */
std::vector<DbusSystemdServer::UnitStruct> DbusSystemdServer::ListUnits()
{
    std::vector<UnitStruct> res;

    for (const auto& [objPath, unit] : m_units) {
        res.emplace_back(unit.m_unitName, ""s, ""s, unit.m_activeState, unit.m_subState, ""s, unit.m_object->getObjectPath(), 0, ""s, "/dummy"s);
    }

    return res;
}

/** @brief Changes unit state of the unit identified by object path (@p objPath) */
void DbusSystemdServer::changeUnitState(const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState)
{
    if (auto it = m_units.find(objPath); it != m_units.end()) {
        it->second.m_activeState = activeState;
        it->second.m_subState = subState;
        it->second.m_object->emitPropertiesChangedSignal(interfaceUnit, {"ActiveState", "SubState"});
    }
}

DbusSystemdServer::Unit::Unit(std::string unitName, std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState)
    : m_unitName(std::move(unitName))
    , m_object(std::move(object))
    , m_activeState(std::move(activeState))
    , m_subState(std::move(subState))
{
}
