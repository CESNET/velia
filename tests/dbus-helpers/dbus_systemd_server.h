#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

/** @brief Mimics the systemd dbus behaviour */
class DbusSystemdServer {
public:
    using UnitStruct = sdbus::Struct<std::string, std::string, std::string, std::string, std::string, std::string, sdbus::ObjectPath, uint32_t, std::string, sdbus::ObjectPath>;
    using DNSServer = sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>, uint16_t, std::string>;

    explicit DbusSystemdServer(sdbus::IConnection& connection);

    void createUnit(sdbus::IConnection& connection, const std::string& unitName, const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState);
    void changeUnitState(const sdbus::ObjectPath& objPath, const std::string& activeState, const std::string& subState);

    void setDNSEx(std::vector<DNSServer> servers);
    void setFallbackDNSEx(std::vector<DNSServer> servers);

    void setNTP(bool enabled);
    void setRuntimeNTPServers(std::vector<std::string> ntpServers);
    void setSystemNTPServers(std::vector<std::string> ntpServers);
    void setLinkNTPServers(std::vector<std::string> ntpServers);
    void setFallbackNTPServers(std::vector<std::string> ntpServers);
    void setServerName(const std::string& serverName);

private:
    struct Unit {
        Unit(std::string unitName, std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState);

        std::string m_unitName;
        std::unique_ptr<sdbus::IObject> m_object;
        std::string m_activeState;
        std::string m_subState;
    };

    sdbus::IConnection& m_connection;
    std::unique_ptr<sdbus::IObject> m_systemd1Manager;
    std::unique_ptr<sdbus::IObject> m_resolve1Manager;
    std::unique_ptr<sdbus::IObject> m_timesync1Manager;
    std::unique_ptr<sdbus::IObject> m_timedate1Manager;

    std::map<sdbus::ObjectPath, Unit> m_units;
    std::vector<DNSServer> m_DNSEx, m_FallbackDNSEx;

    std::vector<std::string> m_RuntimeNTPServers;
    std::vector<std::string> m_SystemNTPServers;
    std::vector<std::string> m_LinkNTPServers;
    std::vector<std::string> m_FallbackNTPServers;
    std::string m_ServerName;
    bool m_canNTP, m_NTP;

    std::vector<UnitStruct> ListUnits();
};
