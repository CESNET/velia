/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <fstream>
#include <pwd.h>
#include <shadow.h>
#include <spdlog/spdlog.h>
#include "Authentication.h"
#include "utils/exec.h"
#include "utils/libyang.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;
namespace {
const auto czechlight_system_module = "czechlight-system"s;
const auto authentication_container = "/" + czechlight_system_module + ":authentication";
const auto change_password_action = "/" + czechlight_system_module + ":authentication/users/change-password";
const auto add_key_action = "/" + czechlight_system_module + ":authentication/users/add-authorized-key";
const auto remove_key_action = "/" + czechlight_system_module + ":authentication/users/authorized-keys/remove";
}

namespace velia::system::NAME_THIS {
// TODO: how to test this one
void changePassword(const std::string& name, const std::string& password)
{
    utils::execAndWait(spdlog::get("system"), "chpasswd", {}, name + ":" + password);
}

namespace {
auto listKeys(const std::string& username)
{
    std::vector<std::string> res;
    // TODO: make this configurable somehow
    std::ifstream ifs("/home/" + username + "/.ssh/authorized_keys");
    std::string line;
    while (std::getline(ifs, line)) {
        // FIXME: is this enough find lines with just whitespace?
        if (line.find_first_not_of(" \r\t") == std::string::npos) {
            continue;
        }

        res.emplace_back(line);
    }

    return res;
}

void writeKeys(const std::string& username, const std::vector<std::string>& keys)
{
    // TODO: this needs some sort of a fail safe mechanism, the writing mustn't fail, maybe write to a temp file and then copy?
    std::ofstream ofs("/home/" + username + "/.ssh/authorized_keys");
    if (!ofs) {
        throw std::runtime_error("Error writing authorized_keys file.");
    }
    for (const auto& key : keys) {
        ofs << key << "\n";
    }
}
}

// TODO: how to test this one
std::vector<User> listUsers()
{
    std::vector<User> res;
    setpwent();
    while (auto entry = getpwent()) {
        User user;
        user.name = entry->pw_name;
        auto shadowEntry = getspnam(entry->pw_name);
        user.passwordHash = shadowEntry->sp_pwdp;
        user.authorizedKeys = listKeys(user.name);
        res.emplace_back(user);
    }

    endpwent();
    return res;
}

void addKey(const std::string& username, const std::string& key)
{
    try {
        utils::execAndWait(spdlog::get("system"), "ssh-keygen", {"-l", "-f", "-"}, key);
    } catch (std::runtime_error& ex) {
        throw std::runtime_error("\"" + key + "\" is not a valid key.");
    }
    auto currentKeys = listKeys(username);
    currentKeys.emplace_back(key);
    writeKeys(username, currentKeys);
}

void removeKey(const std::string& username, const int index)
{
    auto currentKeys = listKeys(username);
    if (currentKeys.size() == 1) {
        // FIXME: maybe add an option to bypass this check?
        throw std::runtime_error("Can't remove last key.");
    }
    currentKeys.erase(currentKeys.begin() + index);
    writeKeys(username, currentKeys);
}
}

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
{
    m_log->debug("Initializing authentication");
    utils::ensureModuleImplemented(srSess, "czechlight-system", "2021-01-13");

    sysrepo::OperGetItemsCb listUsers = [logger = m_log, listUsers = std::move(callbacks.listUsers)] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *path,
            [[maybe_unused]] const char *request_xpath,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node& out) {
        logger->debug("Listing users");

        auto users = listUsers();
        logger->trace("got {} users", users.size());
        usersToTree(session->get_context(), users, out);

        return SR_ERR_OK;
    };

    sysrepo::RpcTreeCb changePassword = [logger = m_log, changePassword = std::move(callbacks.changePassword)] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *op_path,
            [[maybe_unused]] const libyang::S_Data_Node input,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node output) {

        auto userNode = getSubtree(input, (authentication_container + "/users" ).c_str());
        auto name = getValueAsString(getSubtree(userNode, "name"));
        auto password = getValueAsString(getSubtree(userNode, "change-password/password-cleartext"));
        logger->info("Changing password for {}", name);
        try {
            changePassword(name, password);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        } catch (std::exception& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        }

        return SR_ERR_OK;

    };

    sysrepo::RpcTreeCb addKey = [logger = m_log, addKey = std::move(callbacks.addKey)] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *op_path,
            [[maybe_unused]] const libyang::S_Data_Node input,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node output) {

        auto userNode = getSubtree(input, (authentication_container + "/users").c_str());
        auto name = getValueAsString(getSubtree(userNode, "name"));
        auto key = getValueAsString(getSubtree(userNode, "add-authorized-key/key"));
        logger->info("Adding key for {}", name);
        try {
            addKey(name, key);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        } catch (std::exception& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        }

        return SR_ERR_OK;

    };

    sysrepo::RpcTreeCb removeKey = [logger = m_log, removeKey = std::move(callbacks.removeKey)] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *op_path,
            [[maybe_unused]] const libyang::S_Data_Node input,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node output) {

        auto userNode = getSubtree(input, (authentication_container + "/users").c_str());
        auto name = getValueAsString(getSubtree(userNode, "name"));
        auto key = std::stol(getValueAsString(getSubtree(userNode, "authorized-keys/index")));
        logger->info("Adding key for {}", name);
        try {
            removeKey(name, key);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        } catch (std::exception& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        }

        return SR_ERR_OK;

    };

    m_sub->oper_get_items_subscribe(czechlight_system_module.c_str(), listUsers, authentication_container.c_str());
    m_sub->rpc_subscribe_tree(change_password_action.c_str(), changePassword);
    m_sub->rpc_subscribe_tree(add_key_action.c_str(), addKey);
    m_sub->rpc_subscribe_tree(remove_key_action.c_str(), removeKey);
}
