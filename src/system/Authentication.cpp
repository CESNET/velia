/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <fmt/core.h>
#include <fstream>
#include <pwd.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include "Authentication.h"
#include "system_vars.h"
#include "utils/exec.h"
#include "utils/io.h"
#include "utils/libyang.h"
#include "utils/sysrepo.h"
#include "utils/time.h"

using namespace std::string_literals;
namespace {
const auto czechlight_system_module = "czechlight-system"s;
const auto authentication_container = "/" + czechlight_system_module + ":authentication";
const auto change_password_action = "/" + czechlight_system_module + ":authentication/users/change-password";
const auto add_key_action = "/" + czechlight_system_module + ":authentication/users/add-authorized-key";
const auto remove_key_action = "/" + czechlight_system_module + ":authentication/users/authorized-keys/remove";
}

namespace velia::system::impl {
namespace {
std::string authorizedKeysPath(std::string_view authorized_keys_format, const std::string& username)
{
    using namespace fmt::literals;
    return fmt::format(authorized_keys_format, "USER"_a=username);
}

void writeKeys(std::string_view authorized_keys_format, const std::string& username, const std::vector<std::string>& keys)
{
    std::ostringstream ss;

    for (const auto& key : keys) {
        ss << key << "\n";
    }
    utils::safeWriteFile(authorizedKeysPath(authorized_keys_format, username), ss.str());
}
}

void changePassword(const std::string& name, const std::string& password)
{
    utils::execAndWait(spdlog::get("system"), "chpasswd", {}, name + ":" + password);
    auto shadow = velia::utils::readFileToString("/etc/shadow");
    utils::safeWriteFile(REAL_ETC_SHADOW_FILE, shadow);
}

std::vector<std::string> listKeys(std::string_view authorized_keys_format, const std::string& username)
{
    std::vector<std::string> res;
    std::ifstream ifs(authorizedKeysPath(authorized_keys_format, username));
    if (!ifs.is_open()) {
        return res;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.find_first_not_of(" \r\t") == std::string::npos) {
            continue;
        }

        res.emplace_back(line);
    }

    return res;
}

std::vector<User> listUsers(std::string_view authorized_keys_format)
{
    std::vector<User> res;
    auto passwdFile = std::fopen("/etc/passwd", "r");
    passwd entryBuf;
    size_t bufLen = 10;
    auto buffer = std::make_unique<char[]>(bufLen);
    passwd* entry;

    while (true) {
        auto ret = fgetpwent_r(passwdFile, &entryBuf, buffer.get(), bufLen, &entry);
        if (ret == ERANGE) {
            bufLen += 100;
            buffer = std::make_unique<char[]>(bufLen);
            continue;
        }

        if (ret == ENOENT) {
            break;
        }
        User user;
        user.name = entry->pw_name;
        user.authorizedKeys = listKeys(authorized_keys_format, user.name);
        res.emplace_back(user);
    }

    fclose(passwdFile);

    return res;
}

void addKey(std::string_view authorized_keys_format, const std::string& username, const std::string& key)
{
    try {
        utils::execAndWait(spdlog::get("system"), "ssh-keygen", {"-l", "-f", "-"}, key, {utils::ExecOptions::DropRoot});
    } catch (std::runtime_error& ex) {
        using namespace fmt::literals;
        throw AuthException(fmt::format("Key is not a valid SSH public key: {stderr}\n{key}", "stderr"_a=ex.what(), "key"_a=key));
    }
    auto currentKeys = listKeys(authorized_keys_format, username);
    currentKeys.emplace_back(key);
    writeKeys(authorized_keys_format, username, currentKeys);
}

void removeKey(std::string_view authorized_keys_format, const std::string& username, const int index)
{
    auto currentKeys = listKeys(authorized_keys_format, username);
    if (currentKeys.size() == 1) {
        // FIXME: maybe add an option to bypass this check?
        throw AuthException("Can't remove last key.");
    }
    currentKeys.erase(currentKeys.begin() + index);
    writeKeys(authorized_keys_format, username, currentKeys);
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

        decltype(user.authorizedKeys)::size_type entries = 0;
        for (const auto& authorizedKey : user.authorizedKeys) {
            auto entry = userNode->new_path(ctx, ("authorized-keys[index='" + std::to_string(entries) + "']").c_str(), nullptr, LYD_ANYDATA_CONSTSTRING, 0);
            entry->new_path(ctx, "public-key", authorizedKey.c_str(), LYD_ANYDATA_CONSTSTRING, 0);
            entries++;
        }

        if (user.lastPasswordChange) {
            userNode->new_path(ctx, "password-last-change", user.lastPasswordChange->c_str(), LYD_ANYDATA_CONSTSTRING, 0);
        }
    }
}

velia::system::Authentication::Authentication(sysrepo::S_Session srSess, std::string_view authorized_keys_format, velia::system::Authentication::Callbacks callbacks)
    : m_session(srSess)
    , m_sub(std::make_shared<sysrepo::Subscribe>(srSess))
    , m_log(spdlog::get("system"))
{
    m_log->debug("Initializing authentication");
    m_log->debug("Using {} as authorized keys format", authorized_keys_format);
    utils::ensureModuleImplemented(srSess, "czechlight-system", "2021-01-13");

    sysrepo::OperGetItemsCb listUsers = [&lastPasswordChanges = m_lastPasswordChangeTimes, authorized_keys_format, logger = m_log, listUsers = std::move(callbacks.listUsers)] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *path,
            [[maybe_unused]] const char *request_xpath,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node& out) {
        logger->debug("Listing users");

        auto users = listUsers(authorized_keys_format);
        for (auto& user : users) {
            if (auto lastPasswordChange = lastPasswordChanges.find(user.name); lastPasswordChange != lastPasswordChanges.end()) {
                user.lastPasswordChange = lastPasswordChange->second;
            }
        }
        logger->trace("got {} users", users.size());
        usersToTree(session->get_context(), users, out);

        return SR_ERR_OK;
    };

    sysrepo::RpcTreeCb changePassword = [&lastPasswordChanges = m_lastPasswordChangeTimes, logger = m_log, changePassword = std::move(callbacks.changePassword)] (
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
            lastPasswordChanges[name] = utils::yangTimeFormat(std::chrono::system_clock::now());
        } catch (std::runtime_error& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        }

        return SR_ERR_OK;
    };

    sysrepo::RpcTreeCb addKey = [authorized_keys_format, logger = m_log, addKey = std::move(callbacks.addKey)] (
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
            addKey(authorized_keys_format, name, key);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        } catch (AuthException& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        }

        return SR_ERR_OK;
    };

    sysrepo::RpcTreeCb removeKey = [authorized_keys_format, logger = m_log, removeKey = std::move(callbacks.removeKey)] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *op_path,
            [[maybe_unused]] const libyang::S_Data_Node input,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node output) {

        auto userNode = getSubtree(input, (authentication_container + "/users").c_str());
        auto name = getValueAsString(getSubtree(userNode, "name"));
        auto key = std::stol(getValueAsString(getSubtree(userNode, "authorized-keys/index")));
        logger->info("Removing key for {}", name);
        try {
            removeKey(authorized_keys_format, name, key);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
        } catch (AuthException& ex) {
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
