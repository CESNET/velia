/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <iostream>
#include <spdlog/spdlog.h>
#include <sstream>
#include "firewall/Firewall.h"

using namespace std::string_literals;
namespace {
const auto ietf_acl_module = "ietf-access-control-list"s;
namespace nodepaths {
const auto ace_comment = "/ietf-access-control-list:acls/acl/aces/ace/name";
const auto ipv4_matches = "/ietf-access-control-list:acls/acl/aces/ace/matches/l3/ipv4/ipv4/source-network/source-ipv4-network/source-ipv4-network";
const auto ipv6_matches = "/ietf-access-control-list:acls/acl/aces/ace/matches/l3/ipv6/ipv6/source-network/source-ipv6-network/source-ipv6-network";
const auto action = "/ietf-access-control-list:acls/acl/aces/ace/actions/forwarding";
}

/**
 * Gets a string value from a node.
 *
 * @param node A libyang data node. Mustn't be nullptr.
 *
 */
const char* getValueAsString(const libyang::S_Data_Node& node)
{
    if (!node || node->schema()->nodetype() != LYS_LEAF) {
        throw std::logic_error("retrieveString: invalid node");
    }

    return libyang::Data_Node_Leaf_List(node).value_str();
}
}

std::string generateNftConfig(velia::Log logger, const libyang::S_Data_Node& tree)
{
    using namespace std::string_view_literals;
    std::ostringstream ss;
    ss << "flush ruleset" << "\n";
    ss << "add table inet filter" << "\n";
    ss << "add chain inet filter acls { type filter hook input priority 0; }\n";
    ss << "add rule inet filter acls ct state established,related accept\n";
    ss << "add rule inet filter acls iif lo accept comment \"Accept any localhost traffic\"\n";

    const std::array skippedNodes{
        // Top-level container - don't care
        "/ietf-access-control-list:acls"sv,
        // ACL container
        "/ietf-access-control-list:acls/acl"sv,
        // ACL name - don't care, we always only have one ACL
        "/ietf-access-control-list:acls/acl/name"sv,
        // ACEs container - don't care
        "/ietf-access-control-list:acls/acl/aces"sv,
        // The type is either ipv4, ipv6, eth (which is disabled by a deviation) or a mix of these. The type is there
        // only for YANG validation and doesn't matter to us, because we check for "ipv4" and "ipv6" container.
        "/ietf-access-control-list:acls/acl/type"sv,
        // These are ignored, because they do not give any meaningful information. They are mostly containers.
        "/ietf-access-control-list:acls/acl/aces/ace"sv,
        "/ietf-access-control-list:acls/acl/aces/ace/matches"sv,
        "/ietf-access-control-list:acls/acl/aces/ace/matches/l3/ipv4/ipv4"sv,
        "/ietf-access-control-list:acls/acl/aces/ace/matches/l3/ipv6/ipv6"sv,
        "/ietf-access-control-list:acls/acl/aces/ace/actions"sv,
    };

    logger->trace("traversing the tree");
    std::string comment;
    std::string match;
    for (auto node : tree->tree_dfs()) {
        auto nodeSchemaPath = node->schema()->path(LYS_PATH_FIRST_PREFIX);
        if (std::any_of(skippedNodes.begin(), skippedNodes.end(), [&nodeSchemaPath] (const auto& skippedNode) { return nodeSchemaPath == skippedNode; })) {
            logger->trace("skipping: {}", node->path());
            continue;
        }

        logger->trace("processing node: data   {}", node->path());
        logger->trace("                 schema {}", nodeSchemaPath);
        if (nodeSchemaPath == nodepaths::ace_comment) {
            // We will use the ACE name as a comment inside the rule. However, the comment must be at the end, so we
            // save it for later.
            comment = getValueAsString(node);
        } else if (nodeSchemaPath == nodepaths::ipv4_matches) {
            // Here we save the ip we're matching against.
            match = " ip saddr "s + getValueAsString(node);
        } else if (nodeSchemaPath == nodepaths::ipv6_matches) {
            // Here we save the ip we're matching against.
            match = " ip6 saddr "s + getValueAsString(node);
        } else if (nodeSchemaPath == nodepaths::action) {
            // Action is the last statement we get, so this is where we create the actual rule.
            ss << "add rule inet filter acls" << match;
            auto action = getValueAsString(node);
            if (action ==  "ietf-access-control-list:accept"sv) {
                ss << " accept";
            } else if (action ==  "ietf-access-control-list:drop"sv) {
                ss << " drop";
            } else if (action ==  "ietf-access-control-list:reject"sv) {
                ss << " reject";
            } else {
                // This should theoretically never happen.
                throw std::logic_error("unsupported ACE action: "s + action);
            }

            // After the action, we only add the comment. This is the end of the rule.
            ss << " comment \"" << comment << "\"\n";
            match = "";
            comment = "";
        } else {
            throw std::logic_error("unsupported node: " + node->path());
        }
    }

    return ss.str();
}

velia::firewall::SysrepoFirewall::SysrepoFirewall(sysrepo::S_Session srSess, NftConfigConsumer consumer)
    : m_session(srSess)
    , m_sub(std::make_shared<sysrepo::Subscribe>(srSess))
    , m_log(spdlog::get("firewall"))
{
    auto lyCtx = m_session->get_context();
    if (!lyCtx->get_module("ietf-access-control-list", "2019-03-04")->implemented()) {
        throw std::runtime_error("ietf-access-control-list@2019-03-04 is not installed in sysrepo.");
    }
    if (!lyCtx->get_module("czechlight-firewall", "2021-01-25")->implemented()) {
        throw std::runtime_error("czechlight-firewall@2021-01-25 is not installed in sysrepo.");
    }

    sysrepo::ModuleChangeCb cb = [logger = m_log, consumer = std::move(consumer)] (
            sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *xpath,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id) {

        logger->debug("new data from sysrepo");
        auto data = session->get_data(("/" + (ietf_acl_module + ":*")).c_str());
        // The data from sysrepo aren't guaranteed to be sorted according to the schema, but generateNftConfig depends
        // on that order.
        // FIXME: when libyang2 becomes available, remove this.
        // https://github.com/sysrepo/sysrepo/issues/2292
        data->schema_sort(1);

        consumer(generateNftConfig(logger, data));

        return SR_ERR_OK;
    };
    m_sub->module_change_subscribe(ietf_acl_module.c_str(), cb, nullptr, 0, SR_SUBSCR_DONE_ONLY);
}
