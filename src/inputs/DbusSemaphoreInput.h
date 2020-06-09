#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include "inputs/AbstractInput.h"
#include "manager/StateManager.h"

namespace cla {

class DbusSemaphoreInput : public AbstractInput {
public:
    DbusSemaphoreInput(std::shared_ptr<AbstractManager> mx, sdbus::IConnection& connection, const std::string& bus, const std::string& objectPath, const std::string& propertyName, const std::string propertyInterface);
    ~DbusSemaphoreInput() override;

private:
    std::shared_ptr<sdbus::IProxy> m_dbusObjectProxy;
    std::string m_propertyName;
    std::string m_propertyInterface;
    cla::Log m_log;
};
}