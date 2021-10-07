#include <fmt/format.h>
#include <libyang/Tree_Data.hpp>
#include "utils/libyang.h"

namespace velia::utils {

const char* getValueAsString(const libyang::S_Data_Node& node)
{
    if (!node || node->schema()->nodetype() != LYS_LEAF) {
        throw std::logic_error("retrieveString: invalid node");
    }

    return libyang::Data_Node_Leaf_List(node).value_str();
}

std::optional<libyang::S_Data_Node> getUniqueSubtree(const libyang::S_Data_Node& start, const char* path)
{
    auto set = start->find_path(path);

    switch(set->number()) {
    case 0:
        return std::nullopt;
    case 1:
        return set->data().front();
    default:
        throw std::runtime_error(fmt::format("getSubtree({}, {}): didn't get exactly one match (got {})", start->path(), path, set->number()));
    }
}
}
