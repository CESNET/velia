/*
 * Copyright (C) 2020 - 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <set>
#include <sysrepo-cpp/Session.hpp>
#include "utils/log-fwd.h"

namespace velia::health {

/**
 * Register
 */
class SystemdUnits {
public:
    SystemdUnits(sysrepo::Session session, sdbus::IConnection& connection);
    SystemdUnits(sysrepo::Session session, sdbus::IConnection& connection, const std::string& busname, const std::string& managerObjectPath, const std::string& managerIface, const std::string& unitIface);
    ~SystemdUnits();

private:
    velia::Log m_log;

    sysrepo::Session m_srSession;

    std::string m_busName;
    std::string m_unitIface;
    std::unique_ptr<sdbus::IProxy> m_proxyManager;

    /** List of registered unit watchers */
    std::map<sdbus::ObjectPath, std::unique_ptr<sdbus::IProxy>> m_proxyUnits;

    /** Current unit state. */
    std::map<std::string, std::pair<std::string, std::string>> m_unitState;

    void registerSystemdUnit(sysrepo::Session session, sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath);
    void onUnitStateChange(const std::string& name, const std::string& activeState, const std::string& nSubState);
};

}
