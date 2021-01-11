#include <sdbus-c++/sdbus-c++.h>
#include "dbus_rauc_server.h"
#include "utils/log-init.h"

using namespace std::literals;

namespace {
const std::string interfaceManager = "de.pengutronix.rauc.Installer";
const std::string objectPathManager = "/";
}

/** @brief Create a dbus server on the connection */
DBusRAUCServer::DBusRAUCServer(sdbus::IConnection& connection, std::string primarySlot, const std::map<std::string, velia::system::RAUC::SlotProperties>& status)
    : m_manager(sdbus::createObject(connection, objectPathManager))
    , m_primarySlot(std::move(primarySlot))
{
    for (const auto& [slotName, slotStatus] : status) {
        std::map<std::string, sdbus::Variant> m;
        for (auto it = slotStatus.begin(); it != slotStatus.end(); ++it) {
            // NOTE: I wanted a for-range loop over the map with structured binding [key, val] but this did not compile with clang++.
            // According to http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0588r1.html:
            //  "If a lambda-expression [...] captures a structured binding (explicitly or implicitly), the program is ill-formed."

            const auto& k = it->first;
            std::visit([&k, &m](auto&& arg) { m.insert(std::make_pair(k, sdbus::Variant(arg))); }, it->second);
        }
        m_status.emplace_back(slotName, m);
    }

    // create manager object
    m_manager->registerMethod("GetSlotStatus").onInterface(interfaceManager).implementedAs([this]() { return m_status; });
    m_manager->registerMethod("GetPrimary").onInterface(interfaceManager).implementedAs([this]() { return m_primarySlot; });
    m_manager->finishRegistration();
}
