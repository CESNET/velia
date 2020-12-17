/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include "health/inputs/AbstractInput.h"
#include "health/manager/StateManager.h"

namespace velia::health {

class DbusSemaphoreInput : public AbstractInput {
public:
    DbusSemaphoreInput(std::shared_ptr<AbstractManager> mx, sdbus::IConnection& connection, const std::string& bus, const std::string& objectPath, const std::string& propertyName, const std::string& propertyInterface);
    ~DbusSemaphoreInput() override;

private:
    std::shared_ptr<sdbus::IProxy> m_dbusObjectProxy;
    std::string m_propertyName;
    std::string m_propertyInterface;
    velia::Log m_log;
};
}
