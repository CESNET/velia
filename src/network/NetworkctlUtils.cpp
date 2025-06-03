#include <nlohmann/json.hpp>
#include "network/NetworkctlUtils.h"
#include "utils/log.h"

namespace velia::network {

/** @brief Expects JSON produced by `networkctl list --json=pretty|short` and returns a list of managed links by systemd-networkd. */
std::vector<std::string> systemdNetworkdManagedLinks(const std::string& jsonData)
{
    auto log = spdlog::get("network");
    auto json = nlohmann::json::parse(jsonData);
    std::vector<std::string> managedInterfaces;

    for (const auto& link : json["Interfaces"]) {
        auto name = link.at("Name").get<std::string>();
        auto state = link.at("AdministrativeState").get<std::string>();
        bool isManaged = state != "unmanaged";

        log->trace("found systemd-networkd link {}, {}managed (administrative state: {})", name, isManaged ? "" : "not ", state);

        if (isManaged) {
            managedInterfaces.emplace_back(name);
        }
    }

    return managedInterfaces;
}
}
