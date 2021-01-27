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
    std::string passwordHash;
    std::vector<std::string> authorizedKeys;
};

// TODO: name this to something "implementations" or completely move these functions somewhere else
namespace NAME_THIS {
    void changePassword(const std::string& name, const std::string& password);
    std::vector<User> listUsers();
    void addKey(const std::string& name, const std::string& key);
    void removeKey(const std::string& name, const int key);
}

class Authentication {
public:
    using ListUsers = std::function<std::vector<User>()>;
    using ChangePassword = std::function<void(const std::string& name, const std::string& password)>;
    using AddKey = std::function<void(const std::string& name, const std::string& key)>;
    using RemoveKey = std::function<void(const std::string& name, const int index)>;
    struct Callbacks {
        ListUsers listUsers;
        ChangePassword changePassword;
        AddKey addKey;
        RemoveKey removeKey;
    };

    Authentication(sysrepo::S_Session srSess, Callbacks callbacks);

private:
    sysrepo::S_Session m_session;
    sysrepo::S_Subscribe m_sub;
    velia::Log m_log;
};
}
