#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

/** @brief Mimics a subset of the systemd's network1 DBus server behaviour
 *
 * https://www.freedesktop.org/software/systemd/man/org.freedesktop.network1.html
 * */
class DbusNetwork1Server {
public:
    struct LinkState {
        std::string name;
        std::string administrativeState;
    };

    explicit DbusNetwork1Server(sdbus::IConnection& connection, const std::vector<LinkState>& links);

private:
    struct LinkDbusObject  {
        size_t id;
        std::string name;
        std::string administrativeState;
        std::unique_ptr<sdbus::IObject> object;
    };

    std::unique_ptr<sdbus::IObject> m_manager;
    std::vector<LinkDbusObject> m_links;
};
