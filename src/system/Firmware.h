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
    Firmware(std::shared_ptr<::sysrepo::Connection> srConn, sdbus::IConnection& dbusConnectionSignals, sdbus::IConnection& dbusConnectionMethods);

private:
    std::shared_ptr<RAUC> m_rauc;
    std::shared_ptr<::sysrepo::Connection> m_srConn;
    std::shared_ptr<::sysrepo::Session> m_srSessionOps, m_srSessionRPC;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribeOps, m_srSubscribeRPC; // must be destroyed before shared_ptr<RAUC>, ensures that the callback won't use already destroyed RAUC object
    std::mutex m_mtx; //! @brief locks access to cached elements that are shared from multiple threads
    std::string m_installStatus, m_installMessage;
    std::map<std::string, std::string> m_slotStatusCache;
    velia::Log m_log;

    std::map<std::string, velia::system::RAUC::SlotProperties> fetchSlotStatus() const;
    std::unique_lock<std::mutex> applySlotStatus(const std::map<std::string, velia::system::RAUC::SlotProperties>& raucStatus);
};
}
