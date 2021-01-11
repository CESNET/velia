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

namespace velia::ietf_system {

class RAUC {
public:
    using SlotStatus = std::map<std::string, std::variant<std::string, uint64_t, uint32_t>>;

    explicit RAUC(sdbus::IConnection& connection);
    std::string getPrimary() const;
    std::map<std::string, SlotStatus> getSlotStatus() const;

private:
    std::shared_ptr<sdbus::IProxy> m_dbusObjectProxy;
    velia::Log m_log;
};

}
