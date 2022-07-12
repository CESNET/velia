/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include "alarms.h"
#include "systemd-alarms.h"
#include "utils/log.h"

namespace velia::health {

/** @brief Construct the systemd unit watcher for arbitrary dbus object. Mainly for tests. */
SystemdUnitAlarms::SystemdUnitAlarms(sysrepo::Session session, const std::set<std::string>& ignoredUnits, sdbus::IConnection& connection, const std::string& busname, const std::string& managerObjectPath, const std::string& managerIface, const std::string& unitIface)
    : m_log(spdlog::get("health"))
    , m_srSession(std::move(session))
    , m_busName(busname)
    , m_unitIface(unitIface)
    , m_proxyManager(sdbus::createProxy(connection, m_busName, managerObjectPath))
{
    // Subscribe to systemd events. Systemd may not generate signals unless explicitly called
    m_proxyManager->callMethod("Subscribe").onInterface(managerIface).withArguments().dontExpectReply();

    // Register to a signal introducing new unit
    m_proxyManager->uponSignal("UnitNew").onInterface(managerIface).call([&](const std::string& unitName, const sdbus::ObjectPath& unitObjectPath) {
        if (m_proxyUnits.find(unitObjectPath) == m_proxyUnits.end()) {
            registerSystemdUnit(connection, unitName, unitObjectPath);
        }
    });
    m_proxyManager->finishRegistration();

    /* Track all current units. Method ListUnits(a(ssssssouso) out) method returns a DBus struct type with information
     * about the unit (see https://www.freedesktop.org/wiki/Software/systemd/dbus/#Manager-ListUnits).
     * In our code we need only the first (index 0, the unit name) field and seventh (index 6, unit object path) field.
     */
    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> units;
    m_proxyManager->callMethod("ListUnits").onInterface(managerIface).storeResultsTo(units);
    for (const auto& unit : units) {
        const auto& unitName = unit.get<0>();
        const auto& unitObjectPath = unit.get<6>();

        if (ignoredUnits.find(unitName) == ignoredUnits.end()) {
            registerSystemdUnit(connection, unitName, unitObjectPath);
        }
    }
}

/** @brief Construct the systemd watcher for well-known systemd paths. */
SystemdUnitAlarms::SystemdUnitAlarms(sysrepo::Session session, const std::set<std::string>& ignoredUnits, sdbus::IConnection& connection)
    : SystemdUnitAlarms(session, ignoredUnits, connection, "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit")
{
}

/** @brief Registers a systemd unit by its unit name and unit dbus objectpath. */
void SystemdUnitAlarms::registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath)
{
    auto proxyUnit = sdbus::createProxy(connection, m_busName, unitObjectPath);
    proxyUnit->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call([&, unitName](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, [[maybe_unused]] const std::vector<std::string>& invalidated) {
        if (iface != m_unitIface) {
            return;
        }

        std::string nActiveState, nSubState;
        if (auto it = changed.find("ActiveState"); it != changed.end()) {
            nActiveState = it->second.get<std::string>();
        }
        if (auto it = changed.find("SubState"); it != changed.end()) {
            nSubState = it->second.get<std::string>();
        }

        onUnitStateChange(unitName, nActiveState, nSubState);
    });
    proxyUnit->finishRegistration();
    m_log->trace("Registered systemd unit watcher for '{}'", unitName);

    // Query the current state of this unit
    std::string nActiveState = proxyUnit->getProperty("ActiveState").onInterface(m_unitIface);
    std::string nSubState = proxyUnit->getProperty("SubState").onInterface(m_unitIface);
    onUnitStateChange(unitName, nActiveState, nSubState);

    m_proxyUnits.emplace(std::make_pair(unitObjectPath, std::move(proxyUnit)));
}

/** @brief Callback for unit state change */
void SystemdUnitAlarms::onUnitStateChange(const std::string& name, const std::string& activeState, const std::string& subState)
{
    auto systemdState = std::make_pair(activeState, subState);

    auto lastState = m_unitState.find(name);
    if (lastState == m_unitState.end()) {
        lastState = m_unitState.insert(std::make_pair(name, systemdState)).first;
    } else if (lastState->second == systemdState) {
        // We were notified about a state change into the same state. No need to fire any events, everything is still the same.
        return;
    }

    std::string alarmSeverity;
    if (activeState == "failed" || (activeState == "activating" && subState == "auto-restart")) {
        alarmSeverity = "major";
    } else {
        alarmSeverity = "cleared";
    }

    m_log->debug("Systemd unit '{}' changed state ({} {})", name, activeState, subState);
    lastState->second = systemdState;

    createOrUpdateAlarm(m_srSession, "czechlight-alarms:systemd-unit-failure", "", name, alarmSeverity, "systemd unit state: (" + activeState + ", " + subState + ")");
}

SystemdUnitAlarms::~SystemdUnitAlarms() = default;

}
