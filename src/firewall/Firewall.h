/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <sysrepo-cpp/Session.hpp>

std::string generateNftConfig(const libyang::S_Data_Node& tree);

class SysrepoFirewall {
public:
    SysrepoFirewall(sysrepo::S_Session srSess);

private:
    sysrepo::S_Session m_session;
    sysrepo::S_Subscribe m_sub;
};
