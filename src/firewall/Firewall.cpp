/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <iostream>
#include <sstream>
#include "firewall/Firewall.h"

using namespace std::string_literals;
namespace {
const auto ietf_acl_module = "ietf-access-control-list"s;

/**
 * Gets a string value from a node.
 *
 * @param node A libyang data node. Mustn't be nullptr. Must be a leaf of type string.
 *
 */
std::string getValueAsString(const libyang::S_Data_Node& node)
{
    if (!node || node->schema()->nodetype() != LYS_LEAF) {
        throw std::logic_error("retrieveString: invalid node");
    }

    return libyang::Data_Node_Leaf_List(node).value_str();
}

/**
 * Gets a subtree at path `path` using find_path starting at `startNode`. If it doesn't exist, returns std::nullopt.
 *
 * @param node A libyang data node. Mustn't be nullptr.
 * @param path Subtree path to find.
 *
 */
std::optional<libyang::S_Data_Node> getSubtree(const libyang::S_Data_Node& startNode, const char* path)
{
    if (!startNode) {
        throw std::logic_error("getSubtree: invalid node");
    }

    auto results = startNode->find_path(path);

    switch (results->number()) {
    case 0:
        return std::nullopt;
    case 1:
        return results->data().front();
    default:
        throw std::logic_error("getSubtree: got multiple nodes");
    }
}

/**
 * Gets all subtrees at path `path` using find_path starting at `startNode`.
 *
 * @param node A libyang data node. Mustn't be nullptr.
 * @param path Subtree path to find.
 *
 */
std::vector<libyang::S_Data_Node> getAllSubtrees(const libyang::S_Data_Node& startNode, const char* path)
{
    if (!startNode) {
        throw std::logic_error("getSubtree: invalid node");
    }

    return startNode->find_path(path)->data();
}
}

std::string generateNftConfig(const libyang::S_Data_Node& tree)
{
    std::ostringstream ss;
    ss << "flush ruleset" << "\n";
    ss << "add table inet filter" << "\n";

    for (const auto& acl : getAllSubtrees(tree, ("/ietf-access-control-list:acls/acl"))) {
        auto aclName = getValueAsString(*getSubtree(acl, "name"));
        ss << "add chain inet filter " << aclName << " { type filter hook input priority 0; }\n";
        ss << "add rule inet filter " << aclName << " ct state established,related accept\n";
        for (const auto& ace : getAllSubtrees(acl, "aces/ace")) {
            ss << "add rule inet filter " << aclName;

            if (auto matchesIPv4 = getSubtree(ace, "matches/ipv4/source-ipv4-network")) {
                ss << " ip saddr " << getValueAsString(*matchesIPv4);
            }

            if (auto matchesIPv6 = getSubtree(ace, "matches/ipv6/source-ipv6-network")) {
                ss << " ip6 saddr " << getValueAsString(*matchesIPv6);
            }

            auto action = getValueAsString(*getSubtree(ace, "actions/forwarding"));
            if (action == "ietf-access-control-list:drop"s) {
                ss << " drop";
            }

            if (action == "ietf-access-control-list:accept"s) {
                ss << " accept";
            }

            ss << "\n";
        }
    }

    return ss.str();
}

SysrepoFirewall::SysrepoFirewall(sysrepo::S_Session srSess, NftConfigConsumer consumer)
    : m_session(srSess)
    , m_sub(std::make_shared<sysrepo::Subscribe>(srSess))
{
    sysrepo::ModuleChangeCb cb = [consumer = std::move(consumer)] (
            sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *xpath,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id) {

        auto data = session->get_data(("/" + (ietf_acl_module + ":*")).c_str());

        consumer(generateNftConfig(data));

        return SR_ERR_OK;
    };
    m_sub->module_change_subscribe(ietf_acl_module.c_str(), cb, nullptr, 0, SR_SUBSCR_DONE_ONLY);
}
