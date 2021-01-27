/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#pragma once

#include <filesystem>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <sysrepo-cpp/Session.hpp>
#include "system/RAUC.h"
#include "utils/log-fwd.h"

namespace velia::system {

class Firmware {
public:
    Firmware(std::shared_ptr<::sysrepo::Connection> srConn, sdbus::IConnection& dbusConnection);

private:
    std::shared_ptr<::sysrepo::Connection> m_srConn;
    std::shared_ptr<::sysrepo::Session> m_srSessionOps, m_srSessionRPC;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribeOps, m_srSubscribeRPC;
    std::shared_ptr<RAUC> m_rauc;
    std::mutex m_mtx; //! @brief locks access to cached elements that are shared from multiple threads
    std::string m_installStatus, m_installMessage;
    velia::Log m_log;
};
}
