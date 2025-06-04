/*
 * Copyright (C) 2020 - 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <set>
#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::health {

/** @brief Watches for systemd units state via DBus and reports their state changes via ietf-alarms */
class SystemdUnits {
public:
    SystemdUnits(sysrepo::Session session, sdbus::IConnection& connection);
    SystemdUnits(sysrepo::Session session, sdbus::IConnection& connection, const std::string& busname, const std::string& managerObjectPath, const std::string& managerIface, const std::string& unitIface);
    ~SystemdUnits();

private:
    struct UnitState {
        std::string activeState;
        std::string subState;

        bool operator==(const UnitState&) const = default;
    };

    enum class RegisterAlarmInventory {
        Yes,
        No,
    };

    velia::Log m_log;

    sysrepo::Session m_srSession;

    std::string m_busName;
    std::string m_unitIface;

    std::mutex m_mtx;

    /** Current unit state. */
    std::map<std::string, UnitState> m_unitState;

    std::unique_ptr<sdbus::IProxy> m_proxyManager;

    /** List of registered unit watchers */
    std::map<sdbus::ObjectPath, std::unique_ptr<sdbus::IProxy>> m_proxyUnits;

    void registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath, const std::optional<UnitState>& unitState, const RegisterAlarmInventory registerAlarmInventory);
    void onUnitStateChange(const std::string& name, const UnitState& unitState);
};

}
