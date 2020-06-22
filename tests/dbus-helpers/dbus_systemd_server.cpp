#include <sdbus-c++/sdbus-c++.h>
#include "dbus_systemd_server.h"
#include "utils/log-init.h"

using namespace std::literals;

DbusSystemdServer::DbusSystemdServer(sdbus::IConnection& connection)
    : m_manager(sdbus::createObject(connection, "/cz/cesnet/systemd1"))
{
    m_manager->registerMethod("Subscribe").onInterface("cz.cesnet.systemd1.Manager").implementedAs([]() {}).withNoReply();
    m_manager->registerMethod("ListUnits").onInterface("cz.cesnet.systemd1.Manager").implementedAs([&]() { return ListUnits(); });
    m_manager->finishRegistration();

    std::vector<std::tuple<std::string, std::string, std::string, std::string>> units {
        {"unit1", "/cz/cesnet/systemd1/unit/unit1", "active", "running"},
        {"unit2", "/cz/cesnet/systemd1/unit/unit2", "activating", "auto-restart"},
        {"unit3", "/cz/cesnet/systemd1/unit/unit3", "failed", "failed"},
    };
    for (const auto& unit : units) {
        auto obj = sdbus::createObject(connection, std::get<1>(unit));
        obj->registerProperty("ActiveState").onInterface("cz.cesnet.systemd1.Unit").withGetter([&]() {
            return "state";
        });
        obj->registerProperty("SubState").onInterface("cz.cesnet.systemd1.Unit").withGetter([&]() {
            return "state";
        });
        obj->finishRegistration();

        m_units.insert(std::make_pair(std::get<0>(unit), Unit(std::move(obj), std::get<2>(unit), std::get<3>(unit))));
    }
}

std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> DbusSystemdServer::ListUnits()
{
    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> res;

    for (const auto& [name, unit] : m_units) {
        res.push_back(sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>(name, ""s, ""s, ""s, ""s, ""s, unit.m_object->getObjectPath(), 0, ""s, "/dummy"s));
    }

    return res;
}

DbusSystemdServer::Unit::Unit(std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState)
    : m_object(std::move(object))
    , m_activeState(std::move(activeState))
    , m_subState(std::move(subState))
{
}