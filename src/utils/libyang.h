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
std::string asString(const libyang::DataNode& node);

/** @brief Gets exactly one node based on `path` starting from `start`. Uses findXPath, so it works even with lists with
 * missing predicates.
 *
 * Throws if there is more than one matching node. Returns std::nullopt if no node matches.
 */
std::optional<libyang::DataNode> getUniqueSubtree(const libyang::DataNode& start, const std::string& path);

/** @brief Format inet:host and port with respect to IPv6 addresses for systemd config files
 *
 * If the host is an IPv6 address and portNode is provided, then the address will be enclosed in square brackets.
 * If portNode is provided, it will be appended after a colon.
 */
std::string formatHostPort(const libyang::DataNode& node, const std::string& hostNode, const std::optional<std::string>& portNode);
}
