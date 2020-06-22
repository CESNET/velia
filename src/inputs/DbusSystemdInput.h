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

class DbusSystemdInput : public AbstractInput {
public:
    DbusSystemdInput(std::shared_ptr<AbstractManager> mx, sdbus::IConnection& connection);
    ~DbusSystemdInput() override;

private:
    velia::Log m_log;

    std::unique_ptr<sdbus::IProxy> m_proxyManager;

    /** List of registered unit watchers */
    std::map<sdbus::ObjectPath, std::unique_ptr<sdbus::IProxy>> m_proxyUnits;

    /** List of units in failed state. */
    std::set<std::string> m_failedUnits;

    void registerSystemdUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& unitObjectPath);
    void onUnitStateChange(const std::string& name, const std::string& activeState, const std::string& nSubState);
};

}