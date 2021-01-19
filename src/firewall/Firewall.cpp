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
}

// FIXME: wow this is not the best code
std::string generateNftConfig(const libyang::S_Data_Node& tree)
{
    std::ostringstream ss;
    ss << "flush ruleset" << "\n";
    ss << "add table inet filter" << "\n";

    std::cout << tree->print_mem(LYD_JSON, LYP_WITHSIBLINGS | LYP_FORMAT);

    for (const auto& acl : tree->find_path("/ietf-access-control-list:acls/acl")->data()) {
        libyang::Data_Node_Leaf_List aclNameNode(acl->find_path("name")->data().front());
        ss << "add chain inet filter " << aclNameNode.value_str() << " { type filter hook input priority 0; }\n";
        ss << "add rule inet filter " << aclNameNode.value_str() << " ct state established,related accept\n";
        for (const auto& ace : acl->find_path("aces/ace")->data()) {
            ss << "add rule inet filter " << aclNameNode.value_str();
            std::cout << "got ACE:";
            auto matchesIPv4 = ace->find_path("matches/ipv4/source-ipv4-network");
            if (matchesIPv4->size() > 0) {
                libyang::Data_Node_Leaf_List leaf = matchesIPv4->data().front();
                std::cout << " matches" << leaf.value_str();
                ss << " ip saddr " << leaf.value_str();
            }
            auto matchesIPv6 = ace->find_path("matches/ipv6/source-ipv6-network");
            if (matchesIPv6->size() > 0) {
                libyang::Data_Node_Leaf_List leaf = matchesIPv6->data().front();
                std::cout << " matches " << leaf.value_str();
                ss << " ip6 saddr " << leaf.value_str();
            }
            libyang::Data_Node_Leaf_List action = ace->find_path("actions/forwarding")->data().front();
            std::cout << " action " << action.value_str() << "\n";

            if (action.value_str() == "ietf-access-control-list:drop"s) {
                ss << " drop";
            }

            if (action.value_str() == "ietf-access-control-list:accept"s) {
                ss << " accept";
            }

            ss << "\n";
        }
    }

    return ss.str();
}

SysrepoFirewall::SysrepoFirewall(sysrepo::S_Session srSess)
    : m_session(srSess)
    , m_sub(std::make_shared<sysrepo::Subscribe>(srSess))
{
    sysrepo::ModuleChangeCb lol = [] (
            sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *xpath,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id) {

        auto data = session->get_data((ietf_acl_module + ":*").c_str());
        std::cout << data->print_mem(LYD_JSON, LYP_WITHSIBLINGS) << "\n";

        return SR_ERR_OK;
    };
    m_sub->module_change_subscribe(ietf_acl_module.c_str(), lol, nullptr, 0, SR_SUBSCR_DONE_ONLY);
}
