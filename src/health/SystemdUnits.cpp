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

    /* Track all current units. Method ListUnits() -> a(ssssssouso) returns a DBus struct type with information
     * about the unit (see https://www.freedesktop.org/wiki/Software/systemd/dbus/#Manager-ListUnits).
     * In our code we need the fields:
     *  - 0: the unit name
     *  - 6: unit object path
     *  - 3: unit activeState
     *  - 4: unit subState
     */
    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> units;
    std::vector<std::string> unitNames;

    // First, fetch all currently loaded units, register to their PropertiesChanged signal and create the alarm-inventory entries in a *single* edit
    m_proxyManager->callMethod("ListUnits").onInterface(managerIface).storeResultsTo(units);
    std::transform(units.begin(), units.end(), std::back_inserter(unitNames), [](const auto& unit) { return unit.template get<0>(); });
    alarms::pushInventory(m_srSession, {{ALARM_ID, ALARM_INVENTORY_DESCRIPTION, unitNames, {ALARM_SEVERITY}}});

    for (const auto& unit : units) {
        registerSystemdUnit(connection, unit.get<0>(), unit.get<6>(), UnitState{unit.get<3>(), unit.get<4>()}, RegisterAlarmInventory::No);
    }

    // Subscribe to systemd events. Systemd may not generate signals unless explicitly called
    m_proxyManager->callMethod("Subscribe").onInterface(managerIface).withArguments().dontExpectReply();

    // Register to a signal introducing new unit. Newly loaded units into systemd can now start coming. The corresponding alarm MUST be registered because it was not yet.
    m_proxyManager->uponSignal("UnitNew").onInterface(managerIface).call([&](const std::string& unitName, const sdbus::ObjectPath& unitObjectPath) {
        registerSystemdUnit(connection, unitName, unitObjectPath, std::nullopt, RegisterAlarmInventory::Yes);
    });
    m_proxyManager->finishRegistration();

    // Ask for all the units once again. There could have been some that were created between the first ListUnits call and the UnitNew subscription
    units.clear();
    m_proxyManager->callMethod("ListUnits").onInterface(managerIface).storeResultsTo(units);
    for (const auto& unit : units) {
        registerSystemdUnit(connection, unit.get<0>(), unit.get<6>(), UnitState{unit.get<3>(), unit.get<4>()}, RegisterAlarmInventory::Yes);
    }
}

/** @brief Construct the systemd watcher for well-known systemd paths. */
SystemdUnits::SystemdUnits(sysrepo::Session session, sdbus::IConnection& connection)
    : SystemdUnits(session, connection, "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit")
{
}

/** @brief Registers a systemd unit by its unit name and unit dbus objectpath. */
void SystemdUnits::registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath, const std::optional<UnitState>& unitState, const RegisterAlarmInventory registerAlarmInventory)
{
    sdbus::IProxy* proxyUnit;

    {
        std::lock_guard lck(m_mtx);
        if (m_proxyUnits.contains(unitObjectPath)) {
            return;
        }

        if (registerAlarmInventory == RegisterAlarmInventory::Yes) {
            alarms::addResourcesToInventory(m_srSession, {{ALARM_ID, {unitName}}});
        }

        proxyUnit = m_proxyUnits.emplace(unitObjectPath, sdbus::createProxy(connection, m_busName, unitObjectPath)).first->second.get();
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

        onUnitStateChange(unitName, UnitState{std::move(newActiveState), std::move(newSubState)});
    });
    proxyUnit->finishRegistration();
    m_log->trace("Registered systemd unit watcher for '{}'", unitName);

    // Query the current state of this unit if not provided
    if (!unitState) {
        std::string newActiveState = proxyUnit->getProperty("ActiveState").onInterface(m_unitIface);
        std::string newSubState = proxyUnit->getProperty("SubState").onInterface(m_unitIface);
        onUnitStateChange(unitName, UnitState{std::move(newActiveState), std::move(newSubState)});
    } else {
        onUnitStateChange(unitName, *unitState);
    }

}

/** @brief Callback for unit state change */
void SystemdUnits::onUnitStateChange(const std::string& name, const UnitState& state)
{
    std::lock_guard lck(m_mtx);
    const auto& [activeState, subState] = state;

    auto lastState = m_unitState.find(name);
    if (lastState == m_unitState.end()) {
        lastState = m_unitState.insert(std::make_pair(name, state)).first;
    } else if (lastState->second == state) {
        // We were notified about a state change into the same state. No need to fire any events, everything is still the same.
        m_log->trace("Systemd unit '{}' changed state but it is the same state as before ({}, {})", name, activeState, subState);
        return;
    }

    std::string alarmSeverity;
    if (activeState == "failed" || (activeState == "activating" && subState == "auto-restart")) {
        alarmSeverity = ALARM_SEVERITY;
    } else {
        alarmSeverity = "cleared";
    }

    m_log->debug("Systemd unit '{}' changed state ({} {})", name, activeState, subState);
    lastState->second = state;

    alarms::push(m_srSession, ALARM_ID, name, alarmSeverity, "systemd unit state: (" + activeState + ", " + subState + ")");
}

SystemdUnits::~SystemdUnits()
{
    std::lock_guard lck(m_mtx);
    m_proxyManager.reset();
    m_proxyUnits.clear();
}
}
