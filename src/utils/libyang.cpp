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
}
