#include <sdbus-c++/sdbus-c++.h>
#include "dbus_systemd_server.h"
#include "utils/log-init.h"

using namespace std::literals;

DbusSystemdServer::DbusSystemdServer(sdbus::IConnection& connection)
    : m_manager(sdbus::createObject(connection, "/cz/cesnet/systemd1"))
{
    // create manager object
    m_manager->registerMethod("Subscribe").onInterface("cz.cesnet.systemd1.Manager").implementedAs([]() {}).withNoReply();
    m_manager->registerMethod("ListUnits").onInterface("cz.cesnet.systemd1.Manager").implementedAs([&]() { return ListUnits(); });
    m_manager->registerSignal("UnitNew").onInterface("cz.cesnet.systemd1.Manager").withParameters<std::string, sdbus::ObjectPath>();
    m_manager->finishRegistration();
}

void DbusSystemdServer::createUnit(sdbus::IConnection& connection, const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState)
{
    m_units.insert(std::make_pair(objPath, Unit(std::move(sdbus::createObject(connection, objPath)), activeState, subState)));

    Unit& unit = m_units.at(objPath);

    unit.m_object->registerProperty("ActiveState").onInterface("cz.cesnet.systemd1.Unit").withGetter([&unit]() {
        return unit.m_activeState;
    });

    unit.m_object->registerProperty("SubState").onInterface("cz.cesnet.systemd1.Unit").withGetter([&unit]() {
        return unit.m_subState;
    });
    unit.m_object->finishRegistration();

    m_manager->emitSignal("UnitNew").onInterface("cz.cesnet.systemd1.Manager").withArguments(std::string(objPath), objPath);
}

std::vector<DbusSystemdServer::UnitStruct> DbusSystemdServer::ListUnits()
{
    std::vector<UnitStruct> res;

    for (const auto& [name, unit] : m_units) {
        res.push_back(UnitStruct(name, ""s, ""s, ""s, ""s, ""s, unit.m_object->getObjectPath(), 0, ""s, "/dummy"s));
    }

    return res;
}

void DbusSystemdServer::changeUnitState(const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState)
{
    if (auto it = m_units.find(objPath); it != m_units.end()) {
        it->second.m_activeState = activeState;
        it->second.m_subState = subState;
        it->second.m_object->emitPropertiesChangedSignal("cz.cesnet.systemd1.Unit", {"ActiveState", "SubState"});
    }
}

DbusSystemdServer::Unit::Unit(std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState)
    : m_object(std::move(object))
    , m_activeState(std::move(activeState))
    , m_subState(std::move(subState))
{
}