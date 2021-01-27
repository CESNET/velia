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

/** @brief Fetches the slot status data from RAUC. Returns an empty map if the fetch fails (RAUC busy, D-Bus error, ...). */
std::map<std::string, velia::system::RAUC::SlotProperties> fetchSlotStatus(std::weak_ptr<velia::system::RAUC> rauc, velia::Log log)
{
    auto r = rauc.lock();
    if (!r) {
        log->trace("Could not fetch RAUC slot status data: RAUC object already destroyed");
        return {};
    }

    try {
        return r->slotStatus();
    } catch (sdbus::Error& e) {
        log->warn("Could not fetch RAUC slot status data: {}", e.getMessage());
        return {};
    }
}

/** @brief Updates the slot status cache with the new data. The function manipulates local cache shared among multiple thread. */
void updateSlotStatus([[maybe_unused]] std::unique_lock<std::mutex>& mtx, std::map<std::string, std::string>& slotStatus, const std::map<std::string, velia::system::RAUC::SlotProperties>& raucStatus)
{
    for (const auto& slotName : FIRMWARE_SLOTS) {
        if (auto it = raucStatus.find(slotName); it != raucStatus.end()) {
            const auto& props = it->second;
            auto xpathPrefix = CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "firmware-slot[name='" + std::get<std::string>(props.at("bootname")) + "']/";

            slotStatus[xpathPrefix + "state"] = std::get<std::string>(props.at("state"));
            slotStatus[xpathPrefix + "version"] = std::get<std::string>(props.at("bundle.version"));
            slotStatus[xpathPrefix + "installed"] = std::get<std::string>(props.at("installed.timestamp"));
            slotStatus[xpathPrefix + "boot-status"] = std::get<std::string>(props.at("boot-status"));
        }
    }
}
}

namespace velia::system {

Firmware::Firmware(std::shared_ptr<::sysrepo::Connection> srConn, sdbus::IConnection& dbusConnectionSignals, sdbus::IConnection& dbusConnectionMethods)
    : m_srConn(std::move(srConn))
    , m_srSessionOps(std::make_shared<::sysrepo::Session>(m_srConn))
    , m_srSessionRPC(std::make_shared<::sysrepo::Session>(m_srConn))
    , m_srSubscribeOps(std::make_shared<::sysrepo::Subscribe>(m_srSessionOps))
    , m_srSubscribeRPC(std::make_shared<::sysrepo::Subscribe>(m_srSessionRPC))
    , m_rauc(std::make_shared<RAUC>(
          dbusConnectionSignals,
          dbusConnectionMethods,
          [this](const std::string& operation) {
              if (operation == "installing") {
                  std::lock_guard<std::mutex> lck(m_mtx);
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

              utils::valuesToYang(data, session, dataNode);
              session->event_notif_send(dataNode);
          },
          [this](int32_t retVal, const std::string& lastError) {
              auto slotStatus = fetchSlotStatus(m_rauc, m_log);

              std::unique_lock<std::mutex> lck(m_mtx);
              updateSlotStatus(lck, m_slotStatus, slotStatus);
              m_installStatus = retVal == 0 ? "succeeded" : "failed";
              m_installMessage = lastError;
          }))
    , m_log(spdlog::get("system"))
{
    {
        auto raucOperation = m_rauc->operation();
        auto raucLastError = m_rauc->lastError();
        auto slotStatus = fetchSlotStatus(m_rauc, m_log);

        std::unique_lock<std::mutex> lck(m_mtx);

        m_installMessage = raucLastError;

        if (raucOperation == "installing") {
            m_installStatus = "in-progress";
        } else if (!raucLastError.empty()) {
            m_installStatus = "failed";
        } else {
            m_installStatus = "none";
        }

        updateSlotStatus(lck, m_slotStatus, slotStatus);
    }

    m_srSubscribeRPC->rpc_subscribe(
        (CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/install").c_str(),
        [this](::sysrepo::S_Session session, [[maybe_unused]] const char* op_path, const ::sysrepo::S_Vals input, [[maybe_unused]] sr_event_t event, [[maybe_unused]] uint32_t request_id, [[maybe_unused]] ::sysrepo::S_Vals_Holder output) {
            auto slotStatus = fetchSlotStatus(m_rauc, m_log);
            {
                std::unique_lock<std::mutex> lck(m_mtx);
                updateSlotStatus(lck, m_slotStatus, slotStatus);
            }

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
        [this](::sysrepo::S_Session session, [[maybe_unused]] const char* module_name, [[maybe_unused]] const char* path, [[maybe_unused]] const char* request_xpath, [[maybe_unused]] uint32_t request_id, libyang::S_Data_Node& parent) {
            std::map<std::string, std::string> data;

            auto slotStatus = fetchSlotStatus(m_rauc, m_log);

            {
                std::unique_lock<std::mutex> lck(m_mtx);
                updateSlotStatus(lck, m_slotStatus, slotStatus);

                data.insert(m_slotStatus.begin(), m_slotStatus.end());
                data[CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status"] = m_installStatus;
                data[CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/message"] = m_installMessage;
            }

            utils::valuesToYang(data, session, parent);
            return SR_ERR_OK;
        },
        (CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "*").c_str(),
        SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
}
}
