#include <arpa/inet.h>
#include <sdbus-c++/sdbus-c++.h>
#include "dbus_resolve1_server.h"
#include "utils/log-init.h"

using namespace std::literals;

namespace {
static const std::string MANAGER_INTERFACE = "org.freedesktop.resolve1.Manager";
static const std::string MANAGER_PATH = "/org/freedesktop/resolve1";
}

DbusResolve1Server::DbusResolve1Server(sdbus::IConnection& connection)
    : m_manager(sdbus::createObject(connection, MANAGER_PATH))
{
    // create manager object
    m_manager->registerProperty("DNSEx").onInterface(MANAGER_INTERFACE).withGetter([this]() { return m_DNSEx; });
    m_manager->registerProperty("FallbackDNSEx").onInterface(MANAGER_INTERFACE).withGetter([this]() { return m_FallbackDNSEx; });
    m_manager->finishRegistration();
}

void DbusResolve1Server::setDNSEx(std::vector<DNSServer> servers)
{
    m_DNSEx = std::move(servers);
}

void DbusResolve1Server::setFallbackDNSEx(std::vector<DNSServer> servers)
{
    m_FallbackDNSEx = std::move(servers);
}
