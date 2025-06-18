#include <nlohmann/json.hpp>
#include <ranges>
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

/** @brief Returns a map of link names to their configuration files for the set links.
 */
std::map<std::string, NetworkConfFiles> linkConfigurationFiles(const std::string& jsonData, std::set<std::string> managedInterfaces)
{
    auto log = spdlog::get("network");
    auto json = nlohmann::json::parse(jsonData);
    std::map<std::string, NetworkConfFiles> result;

    for (const auto& link : json["Interfaces"]) {
        auto name = link.at("Name").get<std::string>();
        if (!managedInterfaces.contains(name)) {
            continue;
        }

        NetworkConfFiles status;

        if (auto it = link.find("NetworkFile"); it != link.end()) {
            status.networkFile = it->get<std::string>();
        }

        if (auto it = link.find("NetworkFileDropins"); it != link.end() && !it->is_null()) {
            // FIXME: C++23 ranges::to: just `... | to<std::vector<std::filesystem::path>()`
            auto dropins = it->get<std::vector<std::string>>() | std::ranges::views::transform([](const std::string& filepath) -> std::filesystem::path { return filepath; });
            status.dropinFiles = {dropins.begin(), dropins.end()};
        }

        result[name] = std::move(status);
        managedInterfaces.erase(name);
    }

    if (!managedInterfaces.empty()) {
        throw std::invalid_argument{"Link " + *managedInterfaces.begin() + " not found in networkctl JSON data"};
    }

    return result;
}
}
