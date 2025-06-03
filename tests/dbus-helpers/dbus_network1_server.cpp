#include <arpa/inet.h>
#include <sdbus-c++/sdbus-c++.h>
#include "dbus_network1_server.h"
#include "utils/log-init.h"

using namespace std::literals;

namespace {
static const std::string LINK_INTERFACE = "org.freedesktop.network1.Link";
static const std::string LINK_PATH_PREFIX = "/org/freedesktop/network1/link";
static const std::string MANAGER_INTERFACE = "org.freedesktop.network1.Manager";
static const std::string MANAGER_PATH = "/org/freedesktop/network1";
}

DbusNetwork1Server::DbusNetwork1Server(sdbus::IConnection& connection, const std::vector<DbusNetwork1Server::LinkState>& links)
    : m_manager(sdbus::createObject(connection, MANAGER_PATH))
{
    m_links.reserve(links.size());

    size_t id = 1;
    for (const auto& link : links) {
        m_links.emplace_back(LinkDbusObject{
            .id = id++,
            .name = link.name,
            .administrativeState = link.administrativeState,
            .object = sdbus::createObject(connection, LINK_PATH_PREFIX + std::to_string(id)),
        });

        auto& linkData = m_links.back();
        linkData.object->registerProperty("AdministrativeState").onInterface(LINK_INTERFACE).withGetter([&linkData]() { return linkData.administrativeState; });
        linkData.object->finishRegistration();
    }

    m_manager->registerMethod("ListLinks").onInterface(MANAGER_INTERFACE).implementedAs([this]() {
        std::vector<sdbus::Struct<int64_t, std::string, sdbus::ObjectPath>> res;
        for (const auto& link : m_links) {
            res.emplace_back(link.id, link.name, link.object->getObjectPath());
        }
        return res;
    });
    m_manager->finishRegistration();
}
