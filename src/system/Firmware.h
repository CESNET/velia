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
#include <sysrepo-cpp/Subscription.hpp>
#include "system/RAUC.h"
#include "utils/log-fwd.h"

namespace velia::system {

class Firmware {
public:
    Firmware(::sysrepo::Connection srConn, sdbus::IConnection& dbusConnectionSignals, sdbus::IConnection& dbusConnectionMethods);

private:
    std::shared_ptr<RAUC> m_rauc;
    std::mutex m_mtx; //! @brief locks access to cached elements that are shared from multiple threads
    std::string m_installStatus, m_installMessage;
    std::map<std::string, std::string> m_slotStatusCache;
    std::map<std::string, std::string> m_bootNameToSlot;
    velia::Log m_log;
    ::sysrepo::Connection m_srConn;
    ::sysrepo::Session m_srSessionOps, m_srSessionRPC;
    /* Subscribe objects must be destroyed before shared_ptr<RAUC> and other objects that are used in the callbacks
     * (m_slotStatusCache, m_installStatus, m_installMessage). If they're not, the objects might be already destroyed
     * while executing the callback.
     */
    std::optional<::sysrepo::Subscription> m_srSubscribeOps, m_srSubscribeRPC;

    std::unique_lock<std::mutex> updateSlotStatus();
};
}
