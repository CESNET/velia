#pragma once

#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include "ietf-system/RAUC.h"

/** @brief Mimics the RAUC DBus behaviour */
class DBusRAUCServer {
public:
    explicit DBusRAUCServer(sdbus::IConnection& connection, std::string primarySlot, const std::map<std::string, velia::ietf_system::RAUC::SlotProperty>& status);

private:
    using DBusSlotStatus = sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>>;

    std::unique_ptr<sdbus::IObject> m_manager;
    std::string m_primarySlot;
    std::vector<DBusSlotStatus> m_status;
};
