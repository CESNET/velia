/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#pragma once
#include <memory>

namespace libyang {
    class Data_Node;
}

/**
 * Gets a string value from a node.
 *
 * @param node A libyang data node. Mustn't be nullptr.
 *
 */
const char* getValueAsString(const std::shared_ptr<libyang::Data_Node>& node);

/** @brief Gets exactly one node based on `path` starting from `start`. Throws if there is more than one matching node. */
std::shared_ptr<libyang::Data_Node> getSubtree(const std::shared_ptr<libyang::Data_Node>& start, const char* path);
