/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#include "DbusSystemdInput.h"
#include "utils/log.h"

namespace velia {

DbusSystemdInput::DbusSystemdInput(std::shared_ptr<AbstractManager> manager, sdbus::IConnection& connection)
    : AbstractInput(std::move(manager))
    , m_log(spdlog::get("input"))
    , m_proxyManager(sdbus::createProxy(connection, "org.freedesktop.systemd1", "/org/freedesktop/systemd1"))
{
    m_proxyManager->callMethod("Subscribe").onInterface("org.freedesktop.systemd1.Manager").dontExpectReply();
    m_proxyManager->uponSignal("UnitNew").onInterface("org.freedesktop.systemd1.Manager").call([&](const std::string& unitName, const sdbus::ObjectPath& unitObjectPath) {
        if (m_proxyUnits.find(unitObjectPath) == m_proxyUnits.end())
            registerSystemdUnit(connection, unitName, unitObjectPath);
    });
    m_proxyManager->finishRegistration();

    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> units;
    m_proxyManager->callMethod("ListUnits").onInterface("org.freedesktop.systemd1.Manager").storeResultsTo(units);

    for (const auto& unit : units) {
        registerSystemdUnit(connection, unit.get<0>(), unit.get<6>());
    }
}

void DbusSystemdInput::registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath)
{
    auto proxyUnit = sdbus::createProxy(connection, "org.freedesktop.systemd1", unitObjectPath);

    proxyUnit->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call([&, unitName](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, [[maybe_unused]] const std::vector<std::string>& invalidated) {
        if (iface != "org.freedesktop.systemd1.Unit") {
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

    std::string nActiveState = proxyUnit->getProperty("ActiveState").onInterface("org.freedesktop.systemd1.Unit");
    std::string nSubState = proxyUnit->getProperty("SubState").onInterface("org.freedesktop.systemd1.Unit");
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