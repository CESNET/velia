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

namespace velia::system {

/** @brief Constructs a class communicating with RAUC via D-Bus.
 *
 * @param signalConnection A D-Bus connection running event loop. Used for handling signals on the object.
 * @param methodConnection A D-Bus connection (does not require to be running event loop). Used for calling D-Bus methods on the object.
 * @param operCb A function to execute whene RAUC's operation status changes.
 * @param progressCb A function to execute when RAUC's installation makes progress.
 * @param completedCb A function to execute when RAUC's installation completes.
 */
RAUC::RAUC(sdbus::IConnection& signalConnection, sdbus::IConnection& methodConnection, std::function<void(const std::string&)> operCb, std::function<void(int32_t, const std::string&)> progressCb, std::function<void(int32_t, const std::string&)> completedCb)
    : m_dbusObjectProxySignals(sdbus::createProxy(signalConnection, BUS, OBJPATH))
    , m_dbusObjectProxyMethods(sdbus::createProxy(methodConnection, BUS, OBJPATH))
    , m_operCb(std::move(operCb))
    , m_progressCb(std::move(progressCb))
    , m_completedCb(std::move(completedCb))
    , m_log(spdlog::get("system"))
{
    m_dbusObjectProxySignals->uponSignal("Completed").onInterface(INTERFACE).call([this](int32_t returnValue) {
        std::string lastError = m_dbusObjectProxySignals->getProperty("LastError").onInterface(INTERFACE);
        m_log->info("InstallBundle completed. Return value {}, last error: '{}'", returnValue, lastError);
        m_completedCb(returnValue, lastError);
    });

    m_dbusObjectProxySignals->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call([this](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, [[maybe_unused]] const std::vector<std::string>& invalidated) {
        if (iface != INTERFACE) {
            return;
        }

        if (auto itProgress = changed.find("Progress"); itProgress != changed.end()) {
            // https://rauc.readthedocs.io/en/v1.4/using.html#sec-processing-progress
            auto progress = itProgress->second.get<sdbus::Struct<int32_t, std::string, int32_t>>();
            int32_t percentage = progress.get<0>();
            std::string message = progress.get<1>();

            m_log->debug("InstallBundle progress changed: {} {}", percentage, message);
            m_progressCb(percentage, message);
        }

        if (auto itOper = changed.find("Operation"); itOper != changed.end()) {
            auto oper = itOper->second.get<std::string>();
            m_log->debug("Operation changed: {}", oper);
            m_operCb(oper);
        }
    });

    m_dbusObjectProxySignals->finishRegistration();
}

/** @brief Get current primary slot.
 *
 * RAUC's DBus GetPrimary method wrapper.
 * See https://rauc.readthedocs.io/en/v1.4/reference.html#the-getprimary-method).
 */
std::string RAUC::primarySlot() const
{
    std::string primarySlot;
    m_dbusObjectProxyMethods->callMethod("GetPrimary").onInterface(INTERFACE).storeResultsTo(primarySlot);
    return primarySlot;
}

/** @brief Get current status of all slots.
 *
 * RAUC's DBus GetSlotStatus method wrapper.
 * The return value is restructualized from sdbus++ data structures to C++ data structures.
 * (see https://rauc.readthedocs.io/en/v1.4/reference.html#gdbus-method-de-pengutronix-rauc-installer-getslotstatus)
 */
std::map<std::string, RAUC::SlotProperties> RAUC::slotStatus() const
{
    std::vector<sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>>> slots;
    m_dbusObjectProxyMethods->callMethod("GetSlotStatus").onInterface(INTERFACE).storeResultsTo(slots);

    std::map<std::string, SlotProperties> res;
    for (const auto& slot : slots) {
        SlotProperties status;

        for (const auto& [key, val] : std::get<1>(slot)) {
            status.insert(std::make_pair(key, sdbusVariantToCPPVariant(val)));
        }

        res.insert(std::make_pair(std::get<0>(slot), status));
    }

    return res;
}

/** @brief Install new bundle.
 *
 * RAUC's DBus InstallBundle method wrapper.
 * This method is non-blocking. The status of the installation progress is announced via DBus properties (LastError, Progress)
 * and after the installation finishes, the Completed signal is triggered.
 * (see https://rauc.readthedocs.io/en/v1.4/reference.html#gdbus-method-de-pengutronix-rauc-installer-installbundle)
 */
void RAUC::install(const std::string& source)
{
    m_dbusObjectProxyMethods->callMethod("InstallBundle").onInterface(INTERFACE).withArguments(source, std::map<std::string, sdbus::Variant> {});
}

std::string RAUC::operation() const
{
    return m_dbusObjectProxyMethods->getProperty("Operation").onInterface(INTERFACE);
}

std::string RAUC::lastError() const
{
    return m_dbusObjectProxyMethods->getProperty("LastError").onInterface(INTERFACE);
}
}
