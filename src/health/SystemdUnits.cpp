/*
 * Copyright (C) 2020 - 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include "SystemdUnits.h"
#include "utils/alarms.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace {
const auto ALARM_ID = "velia-alarms:systemd-unit-failure";
const auto ALARM_SEVERITY = "critical";
const auto ALARM_INVENTORY_DESCRIPTION = "The systemd service is considered in failed state.";
}

namespace velia::health {

/** @brief Construct the systemd unit watcher for arbitrary dbus object. Mainly for tests. */
SystemdUnits::SystemdUnits(sysrepo::Session session, sdbus::IConnection& connection, const std::string& busname, const std::string& managerObjectPath, const std::string& managerIface, const std::string& unitIface)
    : m_log(spdlog::get("health"))
    , m_srSession(std::move(session))
    , m_busName(busname)
    , m_unitIface(unitIface)
    , m_proxyManager(sdbus::createProxy(connection, m_busName, managerObjectPath))
{
    utils::ensureModuleImplemented(m_srSession, "sysrepo-ietf-alarms", "2022-02-17");
    utils::ensureModuleImplemented(m_srSession, "velia-alarms", "2022-07-12");

    utils::createOrUpdateAlarmInventoryEntry(m_srSession, ALARM_ID, std::nullopt, {ALARM_SEVERITY}, true, ALARM_INVENTORY_DESCRIPTION);

    // Subscribe to systemd events. Systemd may not generate signals unless explicitly called
    m_proxyManager->callMethod("Subscribe").onInterface(managerIface).withArguments().dontExpectReply();

    // Register to a signal introducing new unit
    m_proxyManager->uponSignal("UnitNew").onInterface(managerIface).call([&](const std::string& unitName, const sdbus::ObjectPath& unitObjectPath) {
        registerSystemdUnit(connection, unitName, unitObjectPath);
    });
    m_proxyManager->finishRegistration();

    /* Track all current units. Method ListUnits() -> a(ssssssouso) returns a DBus struct type with information
     * about the unit (see https://www.freedesktop.org/wiki/Software/systemd/dbus/#Manager-ListUnits).
     * In our code we need only the first (index 0, the unit name) field and seventh (index 6, unit object path) field.
     */
    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> units;
    m_proxyManager->callMethod("ListUnits").onInterface(managerIface).storeResultsTo(units);
    for (const auto& unit : units) {
        const auto& unitName = unit.get<0>();
        const auto& unitObjectPath = unit.get<6>();

        registerSystemdUnit(connection, unitName, unitObjectPath);
    }
}

/** @brief Construct the systemd watcher for well-known systemd paths. */
SystemdUnits::SystemdUnits(sysrepo::Session session, sdbus::IConnection& connection)
    : SystemdUnits(session, connection, "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit")
{
}

/** @brief Registers a systemd unit by its unit name and unit dbus objectpath. */
void SystemdUnits::registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath)
{
    sdbus::IProxy* proxyUnit;

    {
        std::lock_guard lck(m_mtx);
        if (m_proxyUnits.contains(unitObjectPath)) {
            return;
        }

        proxyUnit = m_proxyUnits.emplace(unitObjectPath, sdbus::createProxy(connection, m_busName, unitObjectPath)).first->second.get();
        utils::addResourceToAlarmInventoryEntry(m_srSession, ALARM_ID, std::nullopt, unitName);
    }

    proxyUnit->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call([&, unitName](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, [[maybe_unused]] const std::vector<std::string>& invalidated) {
        if (iface != m_unitIface) {
            return;
        }

        std::string newActiveState, newSubState;
        if (auto it = changed.find("ActiveState"); it != changed.end()) {
            newActiveState = it->second.get<std::string>();
        }
        if (auto it = changed.find("SubState"); it != changed.end()) {
            newSubState = it->second.get<std::string>();
        }

        onUnitStateChange(unitName, newActiveState, newSubState);
    });
    proxyUnit->finishRegistration();
    m_log->trace("Registered systemd unit watcher for '{}'", unitName);

    // Query the current state of this unit
    std::string newActiveState = proxyUnit->getProperty("ActiveState").onInterface(m_unitIface);
    std::string newSubState = proxyUnit->getProperty("SubState").onInterface(m_unitIface);
    onUnitStateChange(unitName, newActiveState, newSubState);
}

/** @brief Callback for unit state change */
void SystemdUnits::onUnitStateChange(const std::string& name, const std::string& activeState, const std::string& subState)
{
    std::lock_guard lck(m_mtx);

    auto systemdState = std::make_pair(activeState, subState);

    auto lastState = m_unitState.find(name);
    if (lastState == m_unitState.end()) {
        lastState = m_unitState.insert(std::make_pair(name, systemdState)).first;
    } else if (lastState->second == systemdState) {
        // We were notified about a state change into the same state. No need to fire any events, everything is still the same.
        m_log->trace("Systemd unit '{}' changed state but it is the same state as before ({}, {})", name, systemdState.first, systemdState.second);
        return;
    }

    std::string alarmSeverity;
    if (activeState == "failed" || (activeState == "activating" && subState == "auto-restart")) {
        alarmSeverity = ALARM_SEVERITY;
    } else {
        alarmSeverity = "cleared";
    }

    m_log->debug("Systemd unit '{}' changed state ({} {})", name, activeState, subState);
    lastState->second = systemdState;

    utils::createOrUpdateAlarm(m_srSession, ALARM_ID, std::nullopt, name, alarmSeverity, "systemd unit state: (" + activeState + ", " + subState + ")");
}

SystemdUnits::~SystemdUnits() = default;

}