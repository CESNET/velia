/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#pragma once

#include <map>
#include <optional>
#include <string>
#include <sysrepo-cpp/Subscription.hpp>
#include <vector>
#include "utils/log-fwd.h"

namespace velia::system {
struct User {
    std::string name;
    std::vector<std::string> authorizedKeys;
    std::optional<std::string> lastPasswordChange;
};

class AuthException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

namespace impl {
void changePassword(const std::string& name, const std::string& password, const std::string& etc_shadow);
}

class Authentication {
public:
    using ChangePassword = std::function<void(const std::string& name, const std::string& password, const std::string& etc_shadow)>;

    Authentication(sysrepo::Session srSess, const std::string& etc_passwd, const std::string& etc_shadow, const std::string& authorized_keys_format, ChangePassword changePassword);

private:
    std::vector<std::string> listKeys(const std::string& username);
    std::string authorizedKeysPath(const std::string& username);
    std::vector<User> listUsers();
    void addKey(const std::string& username, const std::string& key);
    void removeKey(const std::string& username, const int index);
    std::string homeDirectory(const std::string& username);
    std::map<std::string, std::optional<std::string>> lastPasswordChanges();


    velia::Log m_log;
    std::string m_etc_passwd;
    std::string m_etc_shadow;
    std::string m_authorized_keys_format;
    sysrepo::Session m_session;
    std::optional<sysrepo::Subscription> m_sub;
};
}
