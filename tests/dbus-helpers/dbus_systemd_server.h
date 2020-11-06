#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

/** @brief Mimics the systemd dbus behaviour */
class DbusSystemdServer {
public:
    using UnitStruct = sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>;

    explicit DbusSystemdServer(sdbus::IConnection& connection);

    void createUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState);
    void changeUnitState(const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState);

private:
    struct Unit {
        Unit(std::string unitName, std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState);

        std::string m_unitName;
        std::unique_ptr<sdbus::IObject> m_object;
        std::string m_activeState;
        std::string m_subState;
    };

    std::unique_ptr<sdbus::IObject> m_manager;
    std::map<sdbus::ObjectPath, Unit> m_units;

    std::vector<UnitStruct> ListUnits();
};
