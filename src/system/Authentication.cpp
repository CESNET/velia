/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <fmt/core.h>
#include <fstream>
#include <pwd.h>
#include <shadow.h>
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

namespace velia::system {
namespace {

void writeKeys(const std::string& filename, const std::vector<std::string>& keys)
{
    std::ostringstream ss;

    for (const auto& key : keys) {
        ss << key << "\n";
    }
    utils::safeWriteFile(filename, ss.str());
}
}

namespace impl {
void changePassword(const std::string& name, const std::string& password)
{
    utils::execAndWait(spdlog::get("system"), "chpasswd", {}, name + ":" + password);
    auto shadow = velia::utils::readFileToString("/etc/shadow");
    utils::safeWriteFile(BACKUP_ETC_SHADOW_FILE, shadow);
}
}

std::string Authentication::homeDirectory(const std::string& username)
{
    auto passwdFile = std::fopen(m_etc_passwd.c_str(), "r");
    if (!passwdFile) {
        throw std::runtime_error("can't open passwd file: "s + strerror(errno));
    }
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
        if (ret == 0) {
            if (username == entry->pw_name) {
                return entry->pw_dir;
            } else {
                continue;
            }
        }

        break;
    }

    throw std::runtime_error("User " + username + " doesn't exist");
}

std::optional<std::string> Authentication::lastPasswordChange(const std::string& username)
{
    auto shadowFile = std::fopen(m_etc_shadow.c_str(), "r");
    spwd entryBuf;
    size_t bufLen = 10;
    auto buffer = std::make_unique<char[]>(bufLen);
    spwd* entry;

    while (true) {
        auto ret = fgetspent_r(shadowFile, &entryBuf, buffer.get(), bufLen, &entry);
        if (ret == ERANGE) {
            bufLen += 100;
            buffer = std::make_unique<char[]>(bufLen);
            continue;
        }

        if (ret == 0) {
            if (username == entry->sp_namp) {
                auto l = std::chrono::time_point<std::chrono::system_clock>(std::chrono::hours(24 * entry->sp_lstchg));
                return velia::utils::yangTimeFormat(l);
            } else {
                continue;
            }
        }

        break;
    }

    return std::nullopt;
}

std::string Authentication::authorizedKeysPath(const std::string& username)
{
    using namespace fmt::literals;
    return fmt::format(m_authorized_keys_format, "USER"_a=username, "HOME"_a=homeDirectory(username));
}

std::vector<std::string> Authentication::listKeys(const std::string& username)
{
    std::vector<std::string> res;
    std::ifstream ifs(authorizedKeysPath(username));
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

std::vector<User> Authentication::listUsers()
{
    std::vector<User> res;
    auto passwdFile = std::fopen(m_etc_passwd.c_str(), "r");
    if (!passwdFile) {
        throw std::runtime_error("can't open passwd file: "s + strerror(errno));
    }
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
        user.authorizedKeys = listKeys(user.name);
        user.lastPasswordChange = lastPasswordChange(user.name);
        res.emplace_back(user);
    }

    fclose(passwdFile);

    return res;
}

void Authentication::addKey(const std::string& username, const std::string& key)
{
    try {
        utils::execAndWait(spdlog::get("system"), "ssh-keygen", {"-l", "-f", "-"}, key, {utils::ExecOptions::DropRoot});
    } catch (std::runtime_error& ex) {
        using namespace fmt::literals;
        throw AuthException(fmt::format("Key is not a valid SSH public key: {stderr}\n{key}", "stderr"_a=ex.what(), "key"_a=key));
    }
    auto currentKeys = listKeys(username);
    currentKeys.emplace_back(key);
    writeKeys(authorizedKeysPath(username), currentKeys);
}

void Authentication::removeKey(const std::string& username, const int index)
{
    auto currentKeys = listKeys(username);
    if (currentKeys.size() == 1) {
        // FIXME: maybe add an option to bypass this check?
        throw AuthException("Can't remove last key.");
    }
    currentKeys.erase(currentKeys.begin() + index);
    writeKeys(authorizedKeysPath(username), currentKeys);
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

velia::system::Authentication::Authentication(
        sysrepo::S_Session srSess,
        const std::string& etc_passwd,
        const std::string& etc_shadow,
        const std::string& authorized_keys_format,
        ChangePassword changePassword
    )
    : m_session(srSess)
    , m_sub(std::make_shared<sysrepo::Subscribe>(srSess))
    , m_log(spdlog::get("system"))
    , m_etc_passwd(etc_passwd)
    , m_etc_shadow(etc_shadow)
    , m_authorized_keys_format(authorized_keys_format)
{
    m_log->debug("Initializing authentication");
    m_log->debug("Using {} as passwd file", m_etc_passwd);
    m_log->debug("Using {} as shadow file", m_etc_shadow);
    m_log->debug("Using {} authorized_keys format", m_authorized_keys_format);
    utils::ensureModuleImplemented(srSess, "czechlight-system", "2021-01-13");

    sysrepo::OperGetItemsCb listUsersCb = [this] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *path,
            [[maybe_unused]] const char *request_xpath,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node& out) {
        m_log->debug("Listing users");

        auto users = listUsers();
        m_log->trace("got {} users", users.size());
        usersToTree(session->get_context(), users, out);

        return SR_ERR_OK;
    };

    sysrepo::RpcTreeCb changePasswordCb = [this, changePassword] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *op_path,
            [[maybe_unused]] const libyang::S_Data_Node input,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node output) {

        auto userNode = getSubtree(input, (authentication_container + "/users" ).c_str());
        auto name = getValueAsString(getSubtree(userNode, "name"));
        auto password = getValueAsString(getSubtree(userNode, "change-password/password-cleartext"));
        m_log->debug("Changing password for {}", name);
        try {
            changePassword(name, password);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            m_log->info("Changed password for {}", name);
        } catch (std::runtime_error& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            m_log->info("Failed to change password for {}: {}", name, ex.what());
        }

        return SR_ERR_OK;
    };

    sysrepo::RpcTreeCb addKeyCb = [this] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *op_path,
            [[maybe_unused]] const libyang::S_Data_Node input,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node output) {

        auto userNode = getSubtree(input, (authentication_container + "/users").c_str());
        auto name = getValueAsString(getSubtree(userNode, "name"));
        auto key = getValueAsString(getSubtree(userNode, "add-authorized-key/key"));
        m_log->debug("Adding key for {}", name);
        try {
            addKey(name, key);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            m_log->info("Added a key for {}", name);
        } catch (AuthException& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            m_log->warn("Failed to add a key for {}: {}", name, ex.what());
        }

        return SR_ERR_OK;
    };

    sysrepo::RpcTreeCb removeKeyCb = [this] (
            [[maybe_unused]] sysrepo::S_Session session,
            [[maybe_unused]] const char *op_path,
            [[maybe_unused]] const libyang::S_Data_Node input,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id,
            libyang::S_Data_Node output) {

        auto userNode = getSubtree(input, (authentication_container + "/users").c_str());
        auto name = getValueAsString(getSubtree(userNode, "name"));
        auto key = std::stol(getValueAsString(getSubtree(userNode, "authorized-keys/index")));
        m_log->debug("Removing key for {}", name);
        try {
            removeKey(name, key);
            output->new_path(session->get_context(), "result", "success", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            m_log->info("Removed key for {}", name);
        } catch (AuthException& ex) {
            output->new_path(session->get_context(), "result", "failure", LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            output->new_path(session->get_context(), "message", ex.what(), LYD_ANYDATA_CONSTSTRING, LYD_PATH_OPT_OUTPUT);
            m_log->warn("Failed to remove a key for {}: {}", name, ex.what());
        }

        return SR_ERR_OK;
    };

    m_sub->oper_get_items_subscribe(czechlight_system_module.c_str(), listUsersCb, authentication_container.c_str());
    m_sub->rpc_subscribe_tree(change_password_action.c_str(), changePasswordCb);
    m_sub->rpc_subscribe_tree(add_key_action.c_str(), addKeyCb);
    m_sub->rpc_subscribe_tree(remove_key_action.c_str(), removeKeyCb);
}
