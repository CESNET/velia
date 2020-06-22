#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

/** @brief Mimics the systemd dbus behaviour */
class DbusSystemdServer {
public:
    explicit DbusSystemdServer(sdbus::IConnection& connection);

private:
    struct Unit {
        Unit(std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState);

        std::unique_ptr<sdbus::IObject> m_object;
        std::string m_activeState;
        std::string m_subState;
    };

    std::unique_ptr<sdbus::IObject> m_manager;
    std::map<sdbus::ObjectPath, Unit> m_units;

    std::vector<sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>> ListUnits();
};