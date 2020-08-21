/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#include "DbusSystemdInput.h"
#include "utils/log.h"

namespace velia {

/** @brief Construct the systemd unit watcher for arbitrary dbus object. Mainly for tests. */
DbusSystemdInput::DbusSystemdInput(std::shared_ptr<AbstractManager> manager, sdbus::IConnection& connection, const std::string& busname, const std::string& managerObjectPath, const std::string& managerIface, const std::string& unitIface)
    : AbstractInput(std::move(manager))
    , m_log(spdlog::get("main"))
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

    /* Track all current units. Method ListUnits(a(ssssssouso) out) method returns a dbus struct type with information
     * about the unit (see https://www.freedesktop.org/wiki/Software/systemd/dbus/#Manager-ListUnits).
     * In our code we need only the first (index 0, the unit name) field and seventh (index 6, unit object path) field.
     */
    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> units;
    m_proxyManager->callMethod("ListUnits").onInterface(managerIface).storeResultsTo(units);
    for (const auto& unit : units) {
        registerSystemdUnit(connection, unit.get<0>(), unit.get<6>());
    }
}

/** @brief Construct the systemd watcher for well-known systemd paths. */
DbusSystemdInput::DbusSystemdInput(std::shared_ptr<AbstractManager> manager, sdbus::IConnection& connection)
    : DbusSystemdInput(std::move(manager), connection, "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit")
{
}

/** @brief Registers a systemd unit by its unit name and unit dbus objectpath. */
void DbusSystemdInput::registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath)
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
        m_log->trace("Systemd unit '{}' changed state ({} {})", unitName, nActiveState, nSubState);
    });
    proxyUnit->finishRegistration();

    // Query the current state of this unit
    std::string nActiveState = proxyUnit->getProperty("ActiveState").onInterface(m_unitIface);
    std::string nSubState = proxyUnit->getProperty("SubState").onInterface(m_unitIface);
    onUnitStateChange(unitName, nActiveState, nSubState);

    m_proxyUnits.emplace(std::make_pair(unitObjectPath, std::move(proxyUnit)));
    m_log->trace("Registered systemd unit watcher for '{}' ({} {})", unitName, nActiveState, nSubState);
}

/** @brief Callback for unit state change */
void DbusSystemdInput::onUnitStateChange(const std::string& name, const std::string& activeState, const std::string& subState)
{
    if (activeState == "failed" || (activeState == "activating" && subState == "auto-restart")) {
        m_failedUnits.insert(name); // this unit is in failed state
    } else {
        m_failedUnits.erase(name); // this unit is now OK
    }

    updateState(m_failedUnits.empty() ? State::OK : State::ERROR);
}

DbusSystemdInput::~DbusSystemdInput() = default;

}