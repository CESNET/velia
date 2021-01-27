#include <iostream>
/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <spdlog/spdlog.h>
#include "Authentication.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;
namespace {
const auto czechlight_system_module = "czechlight-system"s;
const auto authentication_container = "/" + czechlight_system_module + ":authentication";
};

void usersToTree(libyang::S_Context ctx, const std::vector<velia::system::User> users, libyang::S_Data_Node& out)
{
    out = std::make_shared<libyang::Data_Node>(
            ctx,
            authentication_container.c_str(),
            nullptr,
            LYD_ANYDATA_CONSTSTRING,
            0);
    for (const auto& user : users) {
        auto userNode = out->new_path(ctx, ("users[name='" + user.name + "']").c_str(), nullptr, LYD_ANYDATA_CONSTSTRING, 0);
        userNode->new_path(ctx, "password", user.passwordHash.c_str(), LYD_ANYDATA_CONSTSTRING, 0);

        decltype(user.authorizedKeys)::size_type entries = 0;
        for (const auto& authorizedKey : user.authorizedKeys) {
            auto entry = userNode->new_path(ctx, ("authorized-keys[index='" + std::to_string(entries) + "']").c_str(), nullptr, LYD_ANYDATA_CONSTSTRING, 0);
            entry->new_path(ctx, "public-key", authorizedKey.c_str(), LYD_ANYDATA_CONSTSTRING, 0);
            entries++;
        }
    }
}


velia::system::Authentication::Authentication(sysrepo::S_Session srSess, velia::system::Authentication::Callbacks callbacks)
    : m_session(srSess)
      , m_sub(std::make_shared<sysrepo::Subscribe>(srSess))
    , m_log(spdlog::get("system"))
      , m_callbacks(callbacks)
{
    utils::ensureModuleImplemented(srSess, "czechlight-system", "2021-01-13");

    sysrepo::OperGetItemsCb cb = [logger = m_log, callbacks = std::move(callbacks)] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *path,
            [[maybe_unused]] const char *request_xpath,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node& out) {

        auto users = callbacks.listUsers();
        usersToTree(session->get_context(), users, out);

        return SR_ERR_OK;
    };


    m_sub->oper_get_items_subscribe(czechlight_system_module.c_str(), cb, authentication_container.c_str());
}
