#pragma once

#include <string>
#include <vector>

namespace velia::network {

std::vector<std::string> systemdNetworkdManagedLinks(const std::string& jsonData);
}
