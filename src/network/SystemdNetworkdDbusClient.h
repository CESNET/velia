#pragma once

#include <sdbus-c++/IProxy.h>
#include "utils/log-fwd.h"

namespace velia::network {
class SystemdNetworkdDbusClient
{
    public:
        SystemdNetworkdDbusClient(sdbus::IConnection& connection, const std::string& busName, const std::string& managerObjectPath);
        std::vector<std::string> getManagedLinks() const;

    private:
        velia::Log m_log;
        std::string m_busName;
        std::unique_ptr<sdbus::IProxy> m_managerObject;
};
}
