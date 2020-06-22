/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#include "DbusSystemdInput.h"
#include "utils/log.h"

namespace velia {

DbusSystemdInput::DbusSystemdInput(std::shared_ptr<AbstractManager> manager, sdbus::IConnection& connection, const std::string& bus, const std::string& managerObjPath, const std::string& managerIface, const std::string& unitIface)
    : AbstractInput(std::move(manager))
    , m_log(spdlog::get("input"))
    , m_busName(bus)
    , m_unitIface(unitIface)
    , m_proxyManager(sdbus::createProxy(connection, m_busName, managerObjPath))
{
    m_log->trace("Systemd: init");

    m_proxyManager->callMethod("Subscribe").onInterface(managerIface).withArguments().dontExpectReply();
    m_log->trace("Systemd: subscribe()");

    m_proxyManager->uponSignal("UnitNew").onInterface(managerIface).call([&](const std::string& unitName, const sdbus::ObjectPath& unitObjectPath) {
        if (m_proxyUnits.find(unitObjectPath) == m_proxyUnits.end())
            registerSystemdUnit(connection, unitName, unitObjectPath);
    });
    m_proxyManager->finishRegistration();
    m_log->trace("Systemd: UnitNew callback registered");

    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> units;
    m_proxyManager->callMethod("ListUnits").onInterface(managerIface).storeResultsTo(units);
    m_log->trace("Systemd: ListUnits done");

    for (const auto& unit : units) {
        registerSystemdUnit(connection, unit.get<0>(), unit.get<6>());
    }
}

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
        m_log->trace("Systemd unit '{}' changed state to {} {}", unitName, nActiveState, nSubState);
    });
    proxyUnit->finishRegistration();

    std::string nActiveState = proxyUnit->getProperty("ActiveState").onInterface(m_unitIface);
    std::string nSubState = proxyUnit->getProperty("SubState").onInterface(m_unitIface);
    onUnitStateChange(unitName, nActiveState, nSubState);
    m_log->trace("Registered systemd unit watcher for '{}' ({} {})", unitName, nActiveState, nSubState);

    // keep the pointer alive
    m_proxyUnits.emplace(std::make_pair(unitObjectPath, std::move(proxyUnit)));
}

void DbusSystemdInput::onUnitStateChange(const std::string& name, const std::string& activeState, const std::string& subState)
{
    if (activeState == "failed" || (activeState == "activating" && subState == "auto-restart")) {
        m_failedUnits.insert(name);
    } else {
        m_failedUnits.erase(name);
    }

    updateState(m_failedUnits.empty() ? State::OK : State::ERROR);
}

DbusSystemdInput::~DbusSystemdInput() = default;

}