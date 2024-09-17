/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <array>
#include <iostream>
#include <spdlog/spdlog.h>
#include <sstream>
#include "firewall/Firewall.h"
#include "utils/libyang.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;
namespace {
const auto ietf_acl_module = "ietf-access-control-list"s;
namespace nodepaths {
const auto ace_comment = "/ietf-access-control-list:acls/acl/aces/ace/name";
const auto ipv4_matches = "/ietf-access-control-list:acls/acl/aces/ace/matches/ipv4/source-ipv4-network";
const auto ipv6_matches = "/ietf-access-control-list:acls/acl/aces/ace/matches/ipv6/source-ipv6-network";
const auto action = "/ietf-access-control-list:acls/acl/aces/ace/actions/forwarding";
}

std::string generateNftConfig(velia::Log logger, const libyang::DataNode& tree, const std::vector<std::filesystem::path>& nftIncludes)
{
    std::ostringstream ss;
    ss << "flush ruleset" << "\n";
    ss << "add table inet filter" << "\n";
    ss << "add chain inet filter acls { type filter hook input priority 0; }\n";
    ss << "add rule inet filter acls ct state established,related accept\n";
    ss << "add rule inet filter acls iif lo accept comment \"Accept any localhost traffic\"\n";

    constexpr std::array skippedNodes{
        // Top-level container - don't care
        "/ietf-access-control-list:acls",
        // ACL container
        "/ietf-access-control-list:acls/acl",
        // ACL name - don't care, we always only have one ACL
        "/ietf-access-control-list:acls/acl/name",
        // ACEs container - don't care
        "/ietf-access-control-list:acls/acl/aces",
        // The type is either ipv4, ipv6, eth (which is disabled by a deviation) or a mix of these. The type is there
        // only for YANG validation and doesn't matter to us, because we check for "ipv4" and "ipv6" container.
        "/ietf-access-control-list:acls/acl/type",
        // These are ignored, because they do not give any meaningful information. They are mostly containers.
        "/ietf-access-control-list:acls/acl/aces/ace",
        "/ietf-access-control-list:acls/acl/aces/ace/matches",
        "/ietf-access-control-list:acls/acl/aces/ace/matches/ipv4",
        "/ietf-access-control-list:acls/acl/aces/ace/matches/ipv6",
        "/ietf-access-control-list:acls/acl/aces/ace/actions",
    };

    logger->trace("traversing the tree");
    std::string comment;
    std::string match;
    for (auto node : tree.childrenDfs()) {
        auto nodeSchemaPath = node.schema().path();
        if (std::any_of(skippedNodes.begin(), skippedNodes.end(), [&nodeSchemaPath] (const auto& skippedNode) { return nodeSchemaPath == skippedNode; })) {
            logger->trace("skipping: {}", node.path());
            continue;
        }

        logger->trace("processing node: data   {}", node.path());
        logger->trace("                 schema {}", nodeSchemaPath);
        if (nodeSchemaPath == nodepaths::ace_comment) {
            // We will use the ACE name as a comment inside the rule. However, the comment must be at the end, so we
            // save it for later.
            comment = velia::utils::asString(node);
        } else if (nodeSchemaPath == nodepaths::ipv4_matches) {
            // Here we save the ip we're matching against.
            match = " ip saddr "s + velia::utils::asString(node);
        } else if (nodeSchemaPath == nodepaths::ipv6_matches) {
            // Here we save the ip we're matching against.
            match = " ip6 saddr "s + velia::utils::asString(node);
        } else if (nodeSchemaPath == nodepaths::action) {
            // Action is the last statement we get, so this is where we create the actual rule.
            ss << "add rule inet filter acls" << match;
            auto action = velia::utils::asString(node);
            if (action == "ietf-access-control-list:accept") {
                ss << " accept";
            } else if (action == "ietf-access-control-list:drop") {
                ss << " drop";
            } else if (action == "ietf-access-control-list:reject") {
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
            throw std::logic_error("unsupported node: "s + node.path());
        }
    }

    for (const auto& path : nftIncludes) {
        ss << "include \"" << path.c_str() << "\"\n";
    }

    return ss.str();
}
}

velia::firewall::SysrepoFirewall::SysrepoFirewall(sysrepo::Session srSess, NftConfigConsumer consumer, const std::vector<std::filesystem::path>& nftIncludeFiles)
    : m_sub()
    , m_log(spdlog::get("firewall"))
{
    auto lyCtx = srSess.getContext();
    utils::ensureModuleImplemented(srSess, "ietf-access-control-list", "2019-03-04");
    utils::ensureModuleImplemented(srSess, "czechlight-firewall", "2021-01-25");

    sysrepo::ModuleChangeCb cb = [nftIncludeFiles, logger = m_log, consumer = std::move(consumer)] (sysrepo::Session session, auto, auto, auto, auto, auto) {
        logger->debug("Applying new data from sysrepo");
        auto data = session.getData("/" + ietf_acl_module + ":*");

        auto config = generateNftConfig(logger, *data, nftIncludeFiles);
        logger->trace("running the consumer...");
        consumer(config);
        logger->trace("consumer done.");

        return sysrepo::ErrorCode::Ok;
    };

    m_sub = srSess.onModuleChange(ietf_acl_module, cb, std::nullopt, 0, sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Enabled);
}
