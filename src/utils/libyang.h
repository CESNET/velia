/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#pragma once
#include <memory>
#include <optional>

namespace libyang {
    class Data_Node;
}

namespace velia::utils {

/**
 * @brief Gets a string value from a node.
 *
 * @param node A libyang data node. Mustn't be nullptr. Must be a leaf.
 *
 */
const char* getValueAsString(const std::shared_ptr<libyang::Data_Node>& node);

/** @brief Gets exactly one node based on `path` starting from `start`.
 *
 * Throws if there is more than one matching node. Returns std::nullopt if no node matches.
 */
std::optional<std::shared_ptr<libyang::Data_Node>> getUniqueSubtree(const std::shared_ptr<libyang::Data_Node>& start, const char* path);
}
