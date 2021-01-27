/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <filesystem>
#include <sdbus-c++/sdbus-c++.h>
#include <sysrepo-cpp/Session.hpp>
#include "system/RAUC.h"
#include "utils/log-fwd.h"

namespace velia::system {

class CzechlightSystem {
public:
    CzechlightSystem(std::shared_ptr<::sysrepo::Connection> srConn, sdbus::IConnection& dbusConnection);

private:
    std::shared_ptr<::sysrepo::Connection> m_srConn;
    std::shared_ptr<::sysrepo::Session> m_srSession;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
    std::shared_ptr<RAUC> m_rauc;
    std::string m_installStatus, m_installMessage;
    std::map<std::string, std::string> m_slotStatus;
    velia::Log m_log;
};
}
