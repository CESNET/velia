/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <regex>
#include "Firmware.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;

namespace {

const auto CZECHLIGHT_SYSTEM_MODULE_NAME = "czechlight-system"s;
const auto CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX = "/"s + CZECHLIGHT_SYSTEM_MODULE_NAME + ":firmware/"s;
const auto FIRMWARE_SLOTS = {"rootfs.0"s, "rootfs.1"s};
// Modified regex of yang:date-and-time
const auto DATE_TIME_REGEX = std::regex{R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)"};
}

namespace velia::system {

Firmware::Firmware(::sysrepo::Connection srConn, sdbus::IConnection& dbusConnectionSignals, sdbus::IConnection& dbusConnectionMethods)
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
            utils::YANGData data = {
                {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/update/message", msg},
                {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/update/progress", std::to_string(perc)},
            };

            std::optional<libyang::DataNode> dataNode;
            auto session = m_srConn.sessionStart();

            utils::valuesToYang(data, {}, {}, session, dataNode);
            session.sendNotification(*dataNode, sysrepo::Wait::No); // No need to wait, it's just a notification.
        },
        [this](int32_t retVal, const std::string& lastError) {
            auto lock = updateSlotStatus();
            m_installStatus = retVal == 0 ? "succeeded" : "failed";
            m_installMessage = lastError;
        }))
    , m_log(spdlog::get("system"))
    , m_srConn(std::move(srConn))
    , m_srSessionOps(m_srConn.sessionStart())
    , m_srSessionRPC(m_srConn.sessionStart())
    , m_srSubscribeOps()
    , m_srSubscribeRPC()
{
    utils::ensureModuleImplemented(m_srSessionOps, "czechlight-system", "2022-07-08");

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

    ::sysrepo::RpcActionCb cbRPC = [this](auto session, auto, auto, auto input, auto, auto, auto) {
        auto lock = updateSlotStatus();

        try {
            auto source = utils::asString(*input.findPath("url"));
            m_rauc->install(source);
        } catch (const sdbus::Error& e) {
            m_log->warn("RAUC install error: '{}'", e.what());
            utils::setErrors(session, e.getMessage());
            return ::sysrepo::ErrorCode::OperationFailed;
        }
        return ::sysrepo::ErrorCode::Ok;
    };

    m_srSubscribeRPC = m_srSessionRPC.onRPCAction(CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/install", cbRPC);

    ::sysrepo::RpcActionCb markSlotAs = [this](auto, auto, auto path, auto input, auto, auto, auto) {
        auto bootName = utils::asString(*input.parent()->findPath("name"));
        std::string action;
        if (path == CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "firmware-slot/set-active-after-reboot") {
            action = "active";
        } else if (path == CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "firmware-slot/set-unhealthy") {
            action = "bad";
        } else {
            throw std::logic_error{"action callback XPath mismatch"};
        }
        std::string slot;
        {
            auto lock = updateSlotStatus();
            auto slotIt = m_bootNameToSlot.find(bootName);
            if (slotIt == m_bootNameToSlot.end()) {
                throw std::runtime_error{"cannot map FW slot boot name '" + bootName + "' to a RAUC slot name"};
            }
            slot = slotIt->second;
        }
        m_log->debug("RAUC: marking boot slot {} ({}) as {}", bootName, slot, action);
        m_rauc->mark(action, slot);
        return ::sysrepo::ErrorCode::Ok;
    };

    m_srSubscribeRPC->onRPCAction(CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "firmware-slot/set-active-after-reboot", markSlotAs);
    m_srSubscribeRPC->onRPCAction(CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "firmware-slot/set-unhealthy", markSlotAs);

    ::sysrepo::OperGetCb cbOper = [this](auto session, auto, auto, auto, auto, auto, auto& parent) {
        velia::utils::YANGData data;

        {
            auto lock = updateSlotStatus();

            for (const auto& [k, v] : m_slotStatusCache) {
                data.emplace_back(k, v);
            }
            data.emplace_back(CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status", m_installStatus);
            data.emplace_back(CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/message", m_installMessage);
        }

        utils::valuesToYang(data, {}, {}, session, parent);
        return ::sysrepo::ErrorCode::Ok;
    };

    m_srSubscribeOps = m_srSessionOps.onOperGet(
        CZECHLIGHT_SYSTEM_MODULE_NAME,
        cbOper,
        CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "*",
        ::sysrepo::SubscribeOptions::Passive | ::sysrepo::SubscribeOptions::OperMerge);
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
    std::string primarySlot;

    try {
        slotStatus = m_rauc->slotStatus();
        primarySlot = m_rauc->primarySlot();
    } catch (const sdbus::Error& e) {
        m_log->warn("Could not fetch RAUC slot status data: {}", e.getMessage());
    }

    std::unique_lock<std::mutex> lck(m_mtx);
    m_bootNameToSlot.clear();

    for (const auto& slotName : FIRMWARE_SLOTS) {
        if (auto it = slotStatus.find(slotName); it != slotStatus.end()) { // if there is an update for the slot "slotName"
            auto& props = it->second;
            std::string xpathPrefix;

            // Better be defensive about provided properties. If somebody removes /slot.raucs, RAUC doesn't provide all the data (at least bundle.version and installed.timestamp).
            if (auto pit = props.find("bootname"); pit != props.end()) {
                xpathPrefix = CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "firmware-slot[name='" + std::get<std::string>(pit->second) + "']/";
                m_bootNameToSlot[std::get<std::string>(pit->second)] = slotName;
            } else {
                m_log->error("RAUC didn't provide 'bootname' property for slot '{}'. Skipping update for that slot.", slotName);
                continue;
            }

            // sysrepo needs the "-00:00" suffix instead of the "Z" suffix.
            if (auto pit = props.find("installed.timestamp"); pit != props.end()) {
                auto& ts = std::get<std::string>(pit->second);
                if (std::regex_match(ts, DATE_TIME_REGEX)) {
                    ts.pop_back(); // Get rid of the "Z".
                    ts.append("-00:00");
                } else {
                    m_log->warn("RAUC provided a timestamp in an unexpected format: {}", ts);
                }
            }

            for (const auto& [yangKey, raucKey] : {std::pair{"version", "bundle.version"}, {"installed", "installed.timestamp"}}) {
                if (auto pit = props.find(raucKey); pit != props.end()) {
                    m_slotStatusCache[xpathPrefix + yangKey] = std::get<std::string>(pit->second);
                } else {
                    m_log->warn("RAUC didn't provide '{}' property for slot '{}'.", raucKey, slotName);
                }
            }

            if (auto pit = props.find("state"); pit != props.end()) {
                m_slotStatusCache[xpathPrefix + "is-booted-now"] = (std::get<std::string>(pit->second) == "booted" ? "true" : "false");
            } else {
                m_log->warn("RAUC didn't provide 'state' property for slot '{}'.", slotName);
            }
            if (auto pit = props.find("boot-status"); pit != props.end()) {
                m_slotStatusCache[xpathPrefix + "is-healthy"] = (std::get<std::string>(pit->second) == "good" ? "true" : "false");
            } else {
                m_log->warn("RAUC didn't provide 'boot-status' property for slot '{}'.", slotName);
            }
            m_slotStatusCache[xpathPrefix + "will-boot-next"] = (slotName == primarySlot ? "true" : "false");

        }
    }

    return lck;
}
}
