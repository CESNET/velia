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

class Authentication {
public:
    using AddUser = std::function<void(const std::string& name, const std::string& password)>;
    using RemoveUser = std::function<void(const std::string& name)>;
    using ListUsers = std::function<std::vector<User>()>;
    struct Callbacks {
        AddUser addUser;
        RemoveUser removeUser;
        ListUsers listUsers;
    };

    Authentication(sysrepo::S_Session srSess, Callbacks callbacks);

private:
    sysrepo::S_Session m_session;
    sysrepo::S_Subscribe m_sub;
    velia::Log m_log;
    Callbacks m_callbacks;
};
}
