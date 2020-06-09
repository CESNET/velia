#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

class DbusSemaphoreServer {
public:
    DbusSemaphoreServer(const std::string& serviceName, const std::string& objectPath, const std::string& propertyName, const std::string& propertyInterface);
    void run(const std::vector<std::pair<std::string, std::chrono::milliseconds>>& sequence);

private:
    std::unique_ptr<sdbus::IConnection> m_connection;
    std::unique_ptr<sdbus::IObject> m_object;
    std::string m_propertyName;
    std::string m_propertyInterface;
    std::string m_semaphoreState;
    std::mutex m_semaphoreStateMtx;

    void runServer(const std::vector<std::pair<std::string, std::chrono::milliseconds>>& sequence);
};