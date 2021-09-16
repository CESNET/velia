#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

/** @brief Mimics a subset of the systemd's resolve1 DBus server behaviour
 *
 * https://www.freedesktop.org/software/systemd/man/org.freedesktop.resolve1.html
 * */
class DbusResolve1Server {
public:
    using DNSServer = sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>, uint16_t, std::string>;

    explicit DbusResolve1Server(sdbus::IConnection& connection);
    void setDNSEx(std::vector<DNSServer> servers);
    void setFallbackDNSEx(std::vector<DNSServer> servers);


private:
    std::unique_ptr<sdbus::IObject> m_manager;
    std::vector<DNSServer> m_DNSEx, m_FallbackDNSEx;
};
