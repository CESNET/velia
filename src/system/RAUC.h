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

    RAUC(sdbus::IConnection& connection, std::function<void(const std::string&)> operCb, std::function<void(int32_t, const std::string&)> progressCb, std::function<void(int32_t, const std::string&)> completedCb);
    std::string primarySlot() const;
    std::map<std::string, SlotProperties> slotStatus() const;
    void install(const std::string& source) const;
    std::string operation() const;
    std::string lastError() const;

private:
    std::shared_ptr<sdbus::IProxy> m_dbusObjectProxy;
    std::function<void(const std::string&)> m_operCb;
    std::function<void(int32_t, const std::string&)> m_progressCb;
    std::function<void(int32_t, const std::string&)> m_completedCb;
    velia::Log m_log;
};

}
