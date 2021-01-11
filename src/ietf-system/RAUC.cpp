/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include "RAUC.h"
#include "utils/log.h"

namespace {

const std::string INTERFACE = "de.pengutronix.rauc.Installer";
const std::string BUS = "de.pengutronix.rauc";
const std::string OBJPATH = "/";

std::variant<std::string, uint64_t, uint32_t> sdbusVariantToCPPVariant(const sdbus::Variant& v)
{
    // see https://www.freedesktop.org/software/systemd/man/sd_bus_message_read.html
    auto peek = v.peekValueType();

    // so far (v1.4), RAUC uses only these types
    if (peek == "s") {
        return v.get<std::string>();
    } else if (peek == "u") {
        return v.get<uint32_t>();
    } else if (peek == "t") {
        return v.get<uint64_t>();
    }

    throw std::invalid_argument("Unimplemented sdbus::variant type readout.");
}

}

namespace velia::ietf_system {

RAUC::RAUC(sdbus::IConnection& connection)
    : m_dbusObjectProxy(sdbus::createProxy(connection, BUS, OBJPATH))
    , m_log(spdlog::get("system"))
{
}

/** @brief Get current primary slot.
 *
 * RAUC's DBus GetPrimary method wrapper.
 * See https://rauc.readthedocs.io/en/v1.4/reference.html#the-getprimary-method).
 */
std::string RAUC::Primary() const
{
    std::string primarySlot;
    m_dbusObjectProxy->callMethod("GetPrimary").onInterface(INTERFACE).storeResultsTo(primarySlot);
    return primarySlot;
}

/** @brief Get current status of all slots.
 *
 * RAUC's DBus GetSlotStatus method wrapper.
 * The return value is restructualized from sdbus++ data structures to C++ data structures.
 * (see https://rauc.readthedocs.io/en/v1.4/reference.html#gdbus-method-de-pengutronix-rauc-installer-getslotstatus)
 */
std::map<std::string, RAUC::SlotProperty> RAUC::SlotStatus() const
{
    std::vector<sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>>> slots;
    m_dbusObjectProxy->callMethod("GetSlotStatus").onInterface(INTERFACE).storeResultsTo(slots);

    std::map<std::string, SlotProperty> res;
    for (const auto& slot : slots) {
        SlotProperty status;

        for (const auto& [key, val] : std::get<1>(slot)) {
            status.insert(std::make_pair(key, sdbusVariantToCPPVariant(val)));
        }

        res.insert(std::make_pair(std::get<0>(slot), status));
    }

    return res;
}

}
