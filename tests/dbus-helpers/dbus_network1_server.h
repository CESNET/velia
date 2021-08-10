#pragma once

#include <memory>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

/** @brief Mimics the systemd-networkd dbus behaviour */
class DbusServer {
public:
    explicit DbusServer(sdbus::IConnection& connection);
    void setLinks(const std::vector<std::pair<int, std::string>>& links);

private:
    std::unique_ptr<sdbus::IObject> m_manager;
    std::vector<sdbus::Struct<int, std::string, sdbus::ObjectPath>> m_links;
};
