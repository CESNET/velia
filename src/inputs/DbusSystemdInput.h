/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <set>
#include "inputs/AbstractInput.h"
#include "manager/StateManager.h"

namespace velia {

/**
 * Register
 */
class DbusSystemdInput : public AbstractInput {
public:
    DbusSystemdInput(std::shared_ptr<AbstractManager> manager, const std::set<std::string>& ignoredUnits, sdbus::IConnection& connection);
    DbusSystemdInput(std::shared_ptr<AbstractManager> manager, const std::set<std::string>& ignoredUnits, sdbus::IConnection& connection, const std::string& busname, const std::string& managerObjectPath, const std::string& managerIface, const std::string& unitIface);
    ~DbusSystemdInput() override;

private:
    velia::Log m_log;

    std::string m_busName;
    std::string m_unitIface;
    std::unique_ptr<sdbus::IProxy> m_proxyManager;

    /** List of registered unit watchers */
    std::map<sdbus::ObjectPath, std::unique_ptr<sdbus::IProxy>> m_proxyUnits;

    /** List of units in failed state. */
    std::set<std::string> m_failedUnits;

    void registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath);
    void onUnitStateChange(const std::string& name, const std::string& activeState, const std::string& nSubState);
};

}
