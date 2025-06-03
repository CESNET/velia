#include "network/SystemdNetworkdDbusClient.h"
#include "utils/log.h"

namespace velia::network {
SystemdNetworkdDbusClient::SystemdNetworkdDbusClient(sdbus::IConnection& connection, const std::string& busName, const std::string& managerObjectPath)
    : m_log(spdlog::get("network"))
    , m_busName(busName)
    , m_managerObject(sdbus::createProxy(connection, busName, managerObjectPath))
{
}

std::vector<std::string> SystemdNetworkdDbusClient::getManagedLinks() const
{
    std::vector<sdbus::Struct<int64_t, std::string, sdbus::ObjectPath>> links;
    m_managerObject->callMethod("ListLinks").onInterface("org.freedesktop.network1.Manager").storeResultsTo(links);

    std::vector<std::string> res;
    for (const auto& link : links) {
        auto linkProxy = sdbus::createProxy(m_managerObject->getConnection(), m_busName, std::get<2>(link));
        std::string administrativeState = linkProxy->getProperty("AdministrativeState").onInterface("org.freedesktop.network1.Link");
        bool isManaged = administrativeState != "unmanaged";

        m_log->trace("found systemd-networkd link {}, {}managed (administrative state: {})", std::get<1>(link), isManaged ? "" : "not ", administrativeState);

        // Only return links which are managed by systemd-networkd
        if (isManaged) {
            res.emplace_back(std::get<1>(link));
        }
    }

    return res;
}
}
