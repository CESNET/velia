/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#pragma once

#include <sysrepo-cpp/Session.hpp>
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
void changePassword(const std::string& name, const std::string& password);
std::vector<User> listUsers(std::string_view authorized_keys_format);
void addKey(std::string_view authorized_keys_format, const std::string& name, const std::string& key);
void removeKey(std::string_view authorized_keys_format, const std::string& name, const int key);
std::vector<std::string> listKeys(std::string_view authorized_keys_format, const std::string& username);
}

class Authentication {
public:
    using ListUsers = std::function<std::vector<User>(std::string_view authorized_keys_format)>;
    using ChangePassword = std::function<void(const std::string& name, const std::string& password)>;
    using AddKey = std::function<void(std::string_view authorized_keys_format, const std::string& name, const std::string& key)>;
    using RemoveKey = std::function<void(std::string_view authorized_keys_format,const std::string& name, const int index)>;
    struct Callbacks {
        ListUsers listUsers;
        ChangePassword changePassword;
        AddKey addKey;
        RemoveKey removeKey;
    };

    Authentication(sysrepo::S_Session srSess, std::string_view authorized_keys_format, Callbacks callbacks);

private:
    sysrepo::S_Session m_session;
    sysrepo::S_Subscribe m_sub;
    velia::Log m_log;
    std::map<std::string, std::string> m_lastPasswordChangeTimes;
};
}
