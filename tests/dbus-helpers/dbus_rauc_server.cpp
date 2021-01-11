#include <sdbus-c++/sdbus-c++.h>
#include "dbus_rauc_server.h"
#include "utils/log-init.h"

using namespace std::literals;

namespace {
const std::string interfaceManager = "de.pengutronix.rauc.Installer";
const std::string objectPathManager = "/";
}

/** @brief Create a dbus server on the connection */
DBusRAUCServer::DBusRAUCServer(sdbus::IConnection& connection, std::string primarySlot, const std::map<std::string, velia::ietf_system::RAUC::SlotStatus>& status)
    : m_manager(sdbus::createObject(connection, objectPathManager))
    , m_primarySlot(std::move(primarySlot))
{
    for (const auto& [slotName, slotStatus] : status) {
        std::map<std::string, sdbus::Variant> m;
        for (const auto& [k, v] : slotStatus) {
            std::visit([&k, &m](auto&& arg) { m.insert(std::make_pair(k, sdbus::Variant(arg))); }, v);
        }
        m_status.emplace_back(slotName, m);
    }

    // create manager object
    m_manager->registerMethod("GetSlotStatus").onInterface(interfaceManager).implementedAs([this]() { return m_status; });
    m_manager->registerMethod("GetPrimary").onInterface(interfaceManager).implementedAs([]() { return "rootfs.0"s; });
    m_manager->finishRegistration();
}
