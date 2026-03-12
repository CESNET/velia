#include <sdbus-c++/sdbus-c++.h>
#include "dbus_systemd_server.h"
#include "utils/log-init.h"

using namespace std::literals;

namespace {
constexpr auto ifaceSystemd1Unit = "org.freedesktop.systemd1.Unit";
constexpr auto ifaceSystemd1Manager = "org.freedesktop.systemd1.Manager";
constexpr auto ifaceResolve1Manager = "org.freedesktop.resolve1.Manager";
constexpr auto ifaceTimesync1Manager = "org.freedesktop.timesync1.Manager";
constexpr auto ifaceTimedate1Manager = "org.freedesktop.timedate1";

constexpr auto pathSystemd1 = "/org/freedesktop/systemd1";
constexpr auto pathResolve1 = "/org/freedesktop/resolve1";
constexpr auto pathTimesync1 = "/org/freedesktop/timesync1";
constexpr auto pathTimedate1 = "/org/freedesktop/timedate1";
}

/** @brief Create a DBus server on the connection */
DbusSystemdServer::DbusSystemdServer(sdbus::IConnection& connection)
    : m_connection(connection)
    , m_systemd1Manager(sdbus::createObject(connection, pathSystemd1))
    , m_resolve1Manager(sdbus::createObject(connection, pathResolve1))
    , m_timedate1Manager(sdbus::createObject(connection, pathTimedate1))
{
    m_systemd1Manager->registerMethod("Subscribe").onInterface(ifaceSystemd1Manager).implementedAs([] {}).withNoReply(); // no-op for us
    m_systemd1Manager->registerMethod("ListUnits").onInterface(ifaceSystemd1Manager).implementedAs([this]() { return ListUnits(); });
    m_systemd1Manager->registerSignal("UnitNew").onInterface(ifaceSystemd1Manager).withParameters<std::string, sdbus::ObjectPath>();
    m_systemd1Manager->finishRegistration();

    m_resolve1Manager->registerProperty("DNSEx").onInterface(ifaceResolve1Manager).withGetter([this]() { return m_DNSEx; });
    m_resolve1Manager->registerProperty("FallbackDNSEx").onInterface(ifaceResolve1Manager).withGetter([this]() { return m_FallbackDNSEx; });
    m_resolve1Manager->finishRegistration();

    m_timedate1Manager->registerProperty("CanNTP").onInterface(ifaceTimedate1Manager).withGetter([this]() { return m_canNTP; });
    m_timedate1Manager->registerProperty("NTP").onInterface(ifaceTimedate1Manager).withGetter([this]() { return m_NTP; });
    m_timedate1Manager->finishRegistration();
}

/** @brief Creates a unit inside the test server. Registers dbus object and emits UnitNew signal. **/
void DbusSystemdServer::createUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState)
{
    m_units.insert(std::make_pair(objPath, Unit(unitName, sdbus::createObject(connection, objPath), activeState, subState)));

    Unit& unit = m_units.at(objPath);

    unit.m_object->registerProperty("ActiveState").onInterface(ifaceSystemd1Unit).withGetter([&unit]() {
        return unit.m_activeState;
    });
    unit.m_object->registerProperty("SubState").onInterface(ifaceSystemd1Unit).withGetter([&unit]() {
        return unit.m_subState;
    });
    unit.m_object->finishRegistration();

    m_systemd1Manager->emitSignal("UnitNew").onInterface(ifaceSystemd1Manager).withArguments(unitName, objPath);
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
        it->second.m_object->emitPropertiesChangedSignal(ifaceSystemd1Unit, {"ActiveState", "SubState"});
    }
}

DbusSystemdServer::Unit::Unit(std::string unitName, std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState)
    : m_unitName(std::move(unitName))
    , m_object(std::move(object))
    , m_activeState(std::move(activeState))
    , m_subState(std::move(subState))
{
}

void DbusSystemdServer::setDNSEx(std::vector<DNSServer> servers)
{
    m_DNSEx = std::move(servers);
}

void DbusSystemdServer::setFallbackDNSEx(std::vector<DNSServer> servers)
{
    m_FallbackDNSEx = std::move(servers);
}

void DbusSystemdServer::setNTP(bool enabled)
{
    m_canNTP = true;
    m_NTP = enabled;

    if (enabled) {
        m_timesync1Manager = sdbus::createObject(m_connection, pathTimesync1);
        m_timesync1Manager->registerProperty("RuntimeNTPServers").onInterface(ifaceTimesync1Manager).withGetter([this]() { return m_RuntimeNTPServers; });
        m_timesync1Manager->registerProperty("SystemNTPServers").onInterface(ifaceTimesync1Manager).withGetter([this]() { return m_SystemNTPServers; });
        m_timesync1Manager->registerProperty("LinkNTPServers").onInterface(ifaceTimesync1Manager).withGetter([this]() { return m_LinkNTPServers; });
        m_timesync1Manager->registerProperty("FallbackNTPServers").onInterface(ifaceTimesync1Manager).withGetter([this]() { return m_FallbackNTPServers; });
        m_timesync1Manager->registerProperty("ServerName").onInterface(ifaceTimesync1Manager).withGetter([this]() { return m_ServerName; });
        m_timesync1Manager->finishRegistration();
    } else {
        m_timesync1Manager.reset();
    }
}

void DbusSystemdServer::setRuntimeNTPServers(std::vector<std::string> ntpServers)
{
    m_RuntimeNTPServers = std::move(ntpServers);
}

void DbusSystemdServer::setSystemNTPServers(std::vector<std::string> ntpServers)
{
    m_SystemNTPServers = std::move(ntpServers);
}

void DbusSystemdServer::setLinkNTPServers(std::vector<std::string> ntpServers)
{
    m_LinkNTPServers = std::move(ntpServers);
}

void DbusSystemdServer::setFallbackNTPServers(std::vector<std::string> ntpServers)
{
    m_FallbackNTPServers = std::move(ntpServers);
}

void DbusSystemdServer::setServerName(const std::string& serverName)
{
    m_ServerName = serverName;
}
