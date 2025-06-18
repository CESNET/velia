#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace velia::network {

std::vector<std::string> systemdNetworkdManagedLinks(const std::string& jsonData);

struct NetworkConfFiles {
    std::optional<std::filesystem::path> networkFile;
    std::vector<std::filesystem::path> dropinFiles;

    bool operator==(const NetworkConfFiles&) const = default;
};
std::map<std::string, NetworkConfFiles> linkConfigurationFiles(const std::string& jsonData, std::set<std::string> managedInterfaces);
}
