#include <fmt/format.h>
#include <libyang/Tree_Data.hpp>
#include "utils/libyang.h"

const char* getValueAsString(const libyang::S_Data_Node& node)
{
    if (!node || node->schema()->nodetype() != LYS_LEAF) {
        throw std::logic_error("retrieveString: invalid node");
    }

    return libyang::Data_Node_Leaf_List(node).value_str();
}

libyang::S_Data_Node getSubtree(const libyang::S_Data_Node& start, const char* path)
{
    auto set = start->find_path(path);
    if (set->number() != 1) {
        throw std::runtime_error(fmt::format("getSubtree({}, {}): didn't get exactly one match (got {})", start->path(), path, set->number()));
    }

    return set->data().front();
}
