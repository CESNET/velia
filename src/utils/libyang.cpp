#include <fmt/format.h>
#include <libyang-cpp/DataNode.hpp>
#include <libyang-cpp/Set.hpp>
#include "utils/libyang.h"

namespace velia::utils {

std::string asString(const libyang::DataNode& node)
{
    if (node.schema().nodeType() != libyang::NodeType::Leaf) {
        throw std::logic_error("retrieveString: invalid node");
    }

    return node.asTerm().valueStr();
}

std::optional<libyang::DataNode> getUniqueSubtree(const libyang::DataNode& start, const std::string& path)
{
    auto set = start.findXPath(path);

    switch(set.size()) {
    case 0:
        return std::nullopt;
    case 1:
        return set.front();
    default:
        throw std::runtime_error(fmt::format("getUniqueSubtree({}, {}): more than one match (got {})", start.path(), path, set.size()));
    }
}

std::string formatHostPort(const libyang::DataNode& node, const std::string& hostNodeName, const std::optional<std::string>& portNodeName)
{
    auto hostNode = node.findPath(hostNodeName);
    auto isIpv6 = hostNode->asTerm().valueType().internalPluginId().find("ipv6") != std::string::npos;
    auto hostValue = asString(*hostNode);

    if (auto portNode = portNodeName ? node.findPath(*portNodeName) : std::nullopt; portNode) {
        auto portValue = asString(*portNode);
        return isIpv6 ? fmt::format("[{}]:{}", hostValue, portValue) : fmt::format("{}:{}", hostValue, portValue);
    }

    return hostValue;
}
}
