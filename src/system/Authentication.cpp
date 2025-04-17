/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <fmt/core.h>
#include <fstream>
#include <libyang-cpp/Time.hpp>
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

    std::filesystem::create_directories(std::filesystem::path(filename).parent_path());

    utils::safeWriteFile(filename, ss.str());
}
}

namespace impl {
void changePassword(const std::string& name, const std::string& password, const std::string& etc_shadow)
{
    utils::execAndWait(spdlog::get("system"), CHPASSWD_EXECUTABLE, {}, name + ":" + password);
    auto shadow = velia::utils::readFileToString(etc_shadow);
    utils::safeWriteFile(BACKUP_ETC_SHADOW_FILE, shadow);
}

auto file_open(const char* filename, const char* mode)
{
    auto res = std::unique_ptr<std::FILE, decltype([](auto fp) { std::fclose(fp); })>(std::fopen(filename, mode));
    if (!res.get()) {
        throw std::system_error{errno, std::system_category(), "fopen("s + filename + ") failed"};
    }
    return res;
}
}

std::string Authentication::homeDirectory(const std::string& username)
{
    auto passwdFile = impl::file_open(m_etc_passwd.c_str(), "r");
    passwd entryBuf;
    size_t bufLen = 10;
    auto buffer = std::make_unique<char[]>(bufLen);
    passwd* entry;

    while (true) {
        auto pos = ftell(passwdFile.get());
        auto ret = fgetpwent_r(passwdFile.get(), &entryBuf, buffer.get(), bufLen, &entry);
        if (ret == ERANGE) {
            bufLen += 100;
            buffer = std::make_unique<char[]>(bufLen);
            fseek(passwdFile.get(), pos, SEEK_SET);
            continue;
        }

        if (ret == ENOENT) {
            break;
        }

        if (ret != 0) {
            throw std::system_error{ret, std::system_category(), "fgetpwent_r() failed"};
        }

        assert(entry);

        if (username != entry->pw_name) {
            continue;
        }

        return entry->pw_dir;
    }

    throw std::runtime_error("User " + username + " doesn't exist");
}

std::map<std::string, std::optional<std::string>> Authentication::lastPasswordChanges()
{
    auto shadowFile = impl::file_open(m_etc_shadow.c_str(), "r");
    spwd entryBuf;
    size_t bufLen = 10;
    auto buffer = std::make_unique<char[]>(bufLen);
    spwd* entry;

    std::map<std::string, std::optional<std::string>> res;
    while (true) {
        auto pos = ftell(shadowFile.get());
        auto ret = fgetspent_r(shadowFile.get(), &entryBuf, buffer.get(), bufLen, &entry);
        if (ret == ERANGE) {
            bufLen += 100;
            buffer = std::make_unique<char[]>(bufLen);
            fseek(shadowFile.get(), pos, SEEK_SET);
            continue;
        }

        if (ret == ENOENT) {
            break;
        }

        if (ret != 0) {
            throw std::system_error{ret, std::system_category(), "fgetspent_r() failed"};
        }

        assert(entry);

        using namespace std::chrono_literals;
        using TimeType = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;
        res.emplace(entry->sp_namp, libyang::yangTimeFormat(TimeType(24h * entry->sp_lstchg), libyang::TimezoneInterpretation::Local));
    }

    return res;
}

std::string Authentication::authorizedKeysPath(const std::string& username)
{
    using namespace fmt::literals;
    return fmt::format(fmt::runtime(m_authorized_keys_format), "USER"_a=username, "HOME"_a=homeDirectory(username));
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
    auto passwdFile = impl::file_open(m_etc_passwd.c_str(), "r");
    passwd entryBuf;
    size_t bufLen = 10;
    auto buffer = std::make_unique<char[]>(bufLen);
    passwd* entry;

    auto pwChanges = lastPasswordChanges();
    while (true) {
        auto pos = ftell(passwdFile.get());
        auto ret = fgetpwent_r(passwdFile.get(), &entryBuf, buffer.get(), bufLen, &entry);
        if (ret == ERANGE) {
            bufLen += 100;
            buffer = std::make_unique<char[]>(bufLen);
            fseek(passwdFile.get(), pos, SEEK_SET);
            continue;
        }

        if (ret == ENOENT) {
            break;
        }

        if (ret != 0) {
            throw std::system_error{ret, std::system_category(), "fgetpwent_r() failed"};
        }

        assert(entry);
        User user;
        user.name = entry->pw_name;
        user.authorizedKeys = listKeys(user.name);
        if (auto it = pwChanges.find(user.name); it != pwChanges.end()) {
            user.lastPasswordChange = it->second;
        }
        res.emplace_back(user);
    }

    return res;
}

void Authentication::addKey(const std::string& username, const std::string& key)
{
    try {
        utils::execAndWait(spdlog::get("system"), SSH_KEYGEN_EXECUTABLE, {"-l", "-f", "-"}, key, {utils::ExecOptions::DropRoot});
    } catch (const std::runtime_error& ex) {
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

void usersToTree(libyang::Context ctx, const std::vector<velia::system::User> users, std::optional<libyang::DataNode>& out)
{
    out = ctx.newPath(authentication_container);
    for (const auto& user : users) {
        auto userNode = out->newPath("users[name='" + user.name + "']", std::nullopt);

        decltype(user.authorizedKeys)::size_type entries = 0;
        for (const auto& authorizedKey : user.authorizedKeys) {
            auto entry = userNode->newPath("authorized-keys[index='" + std::to_string(entries) + "']");
            entry->newPath("public-key", authorizedKey);
            entries++;
        }

        if (user.lastPasswordChange) {
            userNode->newPath("password-last-change", user.lastPasswordChange);
        }
    }
}

velia::system::Authentication::Authentication(
        sysrepo::Session srSess,
        const std::string& etc_passwd,
        const std::string& etc_shadow,
        const std::string& authorized_keys_format,
        ChangePassword changePassword
    )
    : m_log(spdlog::get("system"))
    , m_etc_passwd(etc_passwd)
    , m_etc_shadow(etc_shadow)
    , m_authorized_keys_format(authorized_keys_format)
    , m_session(srSess)
    , m_sub()
{
    m_log->debug("Initializing authentication");
    m_log->debug("Using {} as passwd file", m_etc_passwd);
    m_log->debug("Using {} as shadow file", m_etc_shadow);
    m_log->debug("Using {} authorized_keys format", m_authorized_keys_format);
    utils::ensureModuleImplemented(srSess, "czechlight-system", "2022-07-08");

    sysrepo::OperGetCb listUsersCb = [this] (auto session, auto, auto, auto, auto, auto, auto& out) {
        m_log->debug("Listing users");

        auto users = listUsers();
        m_log->trace("got {} users", users.size());
        usersToTree(session.getContext(), users, out);

        return sysrepo::ErrorCode::Ok;
    };

    sysrepo::RpcActionCb changePasswordCb = [this, changePassword] (auto, auto, auto, auto input, auto, auto, auto output) {

        auto userNode = utils::getUniqueSubtree(input, authentication_container + "/users").value();
        auto name = utils::asString(utils::getUniqueSubtree(userNode, "name").value());
        auto password = utils::asString(utils::getUniqueSubtree(userNode, "change-password/password-cleartext").value());
        m_log->debug("Changing password for {}", name);
        try {
            changePassword(name, password, m_etc_shadow);
            output.newPath("result", "success", libyang::CreationOptions::Output);
            m_log->info("Changed password for {}", name);
        } catch (const std::runtime_error& ex) {
            output.newPath("result", "failure", libyang::CreationOptions::Output);
            output.newPath("message", ex.what(), libyang::CreationOptions::Output);
            m_log->info("Failed to change password for {}: {}", name, ex.what());
        }

        return sysrepo::ErrorCode::Ok;
    };

    sysrepo::RpcActionCb addKeyCb = [this] (auto, auto, auto, auto input, auto, auto, auto output) {

        auto userNode = utils::getUniqueSubtree(input, authentication_container + "/users").value();
        auto name = utils::asString(utils::getUniqueSubtree(userNode, "name").value());
        auto key = utils::asString(utils::getUniqueSubtree(userNode, "add-authorized-key/key").value());
        m_log->debug("Adding key for {}", name);
        try {
            addKey(name, key);
            output.newPath("result", "success", libyang::CreationOptions::Output);
            m_log->info("Added a key for {}", name);
        } catch (const AuthException& ex) {
            output.newPath("result", "failure", libyang::CreationOptions::Output);
            output.newPath("message", ex.what(), libyang::CreationOptions::Output);
            m_log->warn("Failed to add a key for {}: {}", name, ex.what());
        }

        return sysrepo::ErrorCode::Ok;
    };

    sysrepo::RpcActionCb removeKeyCb = [this] (auto, auto, auto, auto input, auto, auto, auto output) {
        auto userNode = utils::getUniqueSubtree(input, authentication_container + "/users").value();
        auto name = utils::asString(utils::getUniqueSubtree(userNode, "name").value());
        auto key = std::stol(utils::asString(utils::getUniqueSubtree(userNode, "authorized-keys/index").value()));
        m_log->debug("Removing key for {}", name);
        try {
            removeKey(name, key);
            output.newPath("result", "success", libyang::CreationOptions::Output);
            m_log->info("Removed key for {}", name);
        } catch (const AuthException& ex) {
            output.newPath("result", "failure", libyang::CreationOptions::Output);
            output.newPath("message", ex.what(), libyang::CreationOptions::Output);
            m_log->warn("Failed to remove a key for {}: {}", name, ex.what());
        }

        return sysrepo::ErrorCode::Ok;
    };

    m_sub = m_session.onOperGet(czechlight_system_module, listUsersCb, authentication_container);
    m_sub->onRPCAction(change_password_action, changePasswordCb);
    m_sub->onRPCAction(add_key_action, addKeyCb);
    m_sub->onRPCAction(remove_key_action, removeKeyCb);
}
