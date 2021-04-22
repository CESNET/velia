/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "Firmware.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;

namespace {

const auto CZECHLIGHT_SYSTEM_MODULE_NAME = "czechlight-system"s;
const auto CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX = "/"s + CZECHLIGHT_SYSTEM_MODULE_NAME + ":firmware/"s;
const auto FIRMWARE_SLOTS = {"rootfs.0"s, "rootfs.1"s};

}

namespace velia::system {

Firmware::Firmware(std::shared_ptr<::sysrepo::Connection> srConn, sdbus::IConnection& dbusConnectionSignals, sdbus::IConnection& dbusConnectionMethods)
    : m_rauc(std::make_shared<RAUC>(
        dbusConnectionSignals,
        dbusConnectionMethods,
        [this](const std::string& operation) {
            if (operation == "installing") {
                std::lock_guard<std::mutex> lck(m_mtx);
                m_installMessage = "";
                m_installStatus = "in-progress";
            }
        },
        [this](int32_t perc, const std::string& msg) {
            std::map<std::string, std::string> data = {
                {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/update/message", msg},
                {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/update/progress", std::to_string(perc)},
            };

            libyang::S_Data_Node dataNode;
            auto session = std::make_shared<::sysrepo::Session>(m_srConn);

            utils::valuesToYang(data, {}, session, dataNode);
            session->event_notif_send(dataNode);
        },
        [this](int32_t retVal, const std::string& lastError) {
            auto lock = updateSlotStatus();
            m_installStatus = retVal == 0 ? "succeeded" : "failed";
            m_installMessage = lastError;
        }))
    , m_log(spdlog::get("system"))
    , m_srConn(std::move(srConn))
    , m_srSessionOps(std::make_shared<::sysrepo::Session>(m_srConn))
    , m_srSessionRPC(std::make_shared<::sysrepo::Session>(m_srConn))
    , m_srSubscribeOps(std::make_shared<::sysrepo::Subscribe>(m_srSessionOps))
    , m_srSubscribeRPC(std::make_shared<::sysrepo::Subscribe>(m_srSessionRPC))
{
    utils::ensureModuleImplemented(m_srSessionOps, "czechlight-system", "2021-01-13");

    {
        auto raucOperation = m_rauc->operation();
        auto raucLastError = m_rauc->lastError();

        auto lock = updateSlotStatus();

        m_installMessage = raucLastError;

        if (raucOperation == "installing") {
            m_installStatus = "in-progress";
        } else if (!raucLastError.empty()) {
            m_installStatus = "failed";
        } else {
            m_installStatus = "none";
        }
    }

    m_srSubscribeRPC->rpc_subscribe(
        (CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/install").c_str(),
        [this](auto session, auto, auto input, auto, auto, [[maybe_unused]] auto output) {
            auto lock = updateSlotStatus();

            try {
                std::string source = input->val(0)->val_to_string();
                m_rauc->install(source);
            } catch (sdbus::Error& e) {
                m_log->warn("RAUC install error: '{}'", e.what());
                session->set_error(e.getMessage().c_str(), nullptr);
                return SR_ERR_OPERATION_FAILED;
            }
            return SR_ERR_OK;
        },
        0,
        SR_SUBSCR_CTX_REUSE);

    m_srSubscribeOps->oper_get_items_subscribe(
        CZECHLIGHT_SYSTEM_MODULE_NAME.c_str(),
        [this](auto session, auto, auto, auto, auto, auto& parent) {
            std::map<std::string, std::string> data;

            {
                auto lock = updateSlotStatus();

                data.insert(m_slotStatusCache.begin(), m_slotStatusCache.end());
                data[CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status"] = m_installStatus;
                data[CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/message"] = m_installMessage;
            }

            utils::valuesToYang(data, {}, session, parent);
            return SR_ERR_OK;
        },
        (CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "*").c_str(),
        SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
}

/** @brief Updates the slot status cache with the new data obtained via RAUC
 *
 * Gets current slot status data from RAUC and updates the local slot status cache if new data are available.
 * The methods manipulates with the local cache which is shared among multiple thread.
 *
 * @return an unique_lock (in locked state) that can be further used to manipulate with the local cache
 * */
std::unique_lock<std::mutex> Firmware::updateSlotStatus()
{
    std::map<std::string, velia::system::RAUC::SlotProperties> slotStatus;

    try {
        slotStatus = m_rauc->slotStatus();
    } catch (sdbus::Error& e) {
        m_log->warn("Could not fetch RAUC slot status data: {}", e.getMessage());
    }

    std::unique_lock<std::mutex> lck(m_mtx);

    for (const auto& slotName : FIRMWARE_SLOTS) {
        if (auto it = slotStatus.find(slotName); it != slotStatus.end()) { // if there is an update for the slot "slotName"
            const auto& props = it->second;
            std::string xpathPrefix;

            // Better be defensive about provided properties. If somebody removes /slot.raucs, RAUC doesn't provide all the data (at least bundle.version and installed.timestamp).
            if (auto pit = props.find("bootname"); pit != props.end()) {
                xpathPrefix = CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "firmware-slot[name='" + std::get<std::string>(pit->second) + "']/";
            } else {
                m_log->error("RAUC didn't provide 'bootname' property for slot '{}'. Skipping update for that slot.");
                continue;
            }

            for (const auto& [yangKey, raucKey] : {std::pair{"state", "state"}, {"boot-status", "boot-status"}, {"version", "bundle.version"}, {"installed", "installed.timestamp"}}) {
                if (auto pit = props.find(raucKey); pit != props.end()) {
                    m_slotStatusCache[xpathPrefix + yangKey] = std::get<std::string>(pit->second);
                } else {
                    m_log->warn("RAUC didn't provide '{}' property for slot '{}'.", raucKey, slotName);
                }
            }
        }
    }

    return lck;
}
}
