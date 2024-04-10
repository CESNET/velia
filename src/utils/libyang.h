/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#pragma once
#include <memory>
#include <optional>
#include <string>

namespace libyang {
    class DataNode;
}

namespace velia::utils {

/**
 * @brief Gets a string value from a node.
 *
 * @param node A libyang data node. Mustn't be nullptr. Must be a leaf.
 *
 */
std::string getValueAsString(const libyang::DataNode& node);

/** @brief Gets exactly one node based on `path` starting from `start`. Uses findXPath, so it works even with lists with
 * missing predicates.
 *
 * Throws if there is more than one matching node. Returns std::nullopt if no node matches.
 */
std::optional<libyang::DataNode> getUniqueSubtree(const libyang::DataNode& start, const std::string& path);
}
