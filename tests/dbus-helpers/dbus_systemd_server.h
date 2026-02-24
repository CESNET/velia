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

    void setNTP(bool canNTP, bool NTP);
    void setNTPServers(std::vector<std::string> ntpServers);
    void setFallbackNTPServers(std::vector<std::string> ntpServers);

private:
    struct Unit {
        Unit(std::string unitName, std::unique_ptr<sdbus::IObject> object, std::string activeState, std::string subState);

        std::string m_unitName;
        std::unique_ptr<sdbus::IObject> m_object;
        std::string m_activeState;
        std::string m_subState;
    };

    std::unique_ptr<sdbus::IObject> m_systemd1Manager;
    std::unique_ptr<sdbus::IObject> m_resolve1Manager;
    std::unique_ptr<sdbus::IObject> m_timesync1Manager;
    std::unique_ptr<sdbus::IObject> m_timedate1Manager;

    std::map<sdbus::ObjectPath, Unit> m_units;
    std::vector<DNSServer> m_DNSEx, m_FallbackDNSEx;

    std::vector<std::string> m_NTPServers, m_FallbackNTPServers;
    bool m_canNTP, m_NTP;

    std::vector<UnitStruct> ListUnits();
};
