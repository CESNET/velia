/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <map>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <variant>
#include "utils/log-fwd.h"

namespace velia::system {

class RAUC {
public:
    using SlotProperties = std::map<std::string, std::variant<std::string, uint64_t, uint32_t>>;

    RAUC(sdbus::IConnection& signalConnection, sdbus::IConnection& methodConnection, std::function<void(const std::string&)> operCb, std::function<void(int32_t, const std::string&)> progressCb, std::function<void(int32_t, const std::string&)> completedCb);
    std::string primarySlot() const;
    std::map<std::string, SlotProperties> slotStatus() const;
    void install(const std::string& source);
    std::string operation() const;
    std::string lastError() const;

private:
    /* We have two objects on two connections here intentionally. On a D-Bus signal PropertyChanged we invoke a callback that does something
     * with Sysrepo's operational datastore in velia::system::Firmware class. While executing this code, sdbus-c++'s mutex (sdbus-c++ src/SdBus.h, sdbusMutex_) is locked.
     * However, this operational datastore change invokes a code, that (again) calls a D-Bus method via sdbus-c++ on the same sdbus::IProxy object.
     * Calling method on this object would require to acquire the same mutex that is already held, which results in deadlock.
     * Therefore, we maintain two separate connections to the same D-Bus object. The first one is used for handling signal callbacks and
     * the second is only for calling the D-Bus methods.
     */
    std::shared_ptr<sdbus::IProxy> m_dbusObjectProxySignals, m_dbusObjectProxyMethods;
    std::function<void(const std::string&)> m_operCb;
    std::function<void(int32_t, const std::string&)> m_progressCb;
    std::function<void(int32_t, const std::string&)> m_completedCb;
    velia::Log m_log;
};

}
