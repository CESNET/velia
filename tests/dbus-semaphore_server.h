#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

class DbusSemaphoreServer {
public:
    DbusSemaphoreServer(const std::string& serviceName, const std::string& objectPath, const std::string& propertyInterface);
    void run(const std::vector<std::string>& sequence);

private:
    std::unique_ptr<sdbus::IConnection> m_connection;
    std::unique_ptr<sdbus::IObject> m_object;
    std::string m_propertyInterface;
    std::string m_semaphoreState;
    std::mutex m_semaphoreStateMtx;

    void runInternal(const std::vector<std::string>& sequence);
};