/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "CzechlightSystem.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;

namespace {

const auto CZECHLIGHT_SYSTEM_MODULE_NAME = "czechlight-system"s;
const auto CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX = "/"s + CZECHLIGHT_SYSTEM_MODULE_NAME + ":firmware/"s;

}

namespace velia::system {

CzechlightSystem::CzechlightSystem(std::shared_ptr<::sysrepo::Connection> srConn, sdbus::IConnection& dbusConnection)
    : m_srConn(std::move(srConn))
    , m_srSession(std::make_shared<::sysrepo::Session>(m_srConn))
    , m_srSubscribe(std::make_shared<::sysrepo::Subscribe>(m_srSession))
    , m_rauc(std::make_shared<RAUC>(
          dbusConnection,
          [this](const std::string& operation) {
              if (operation == "installing") {
                  std::map<std::string, std::string> data {
                      {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status", "in-progress"},
                  };

                  utils::valuesPush(data, std::make_shared<::sysrepo::Session>(m_srConn, SR_DS_OPERATIONAL));
              }
          },
          []([[maybe_unused]] int32_t perc, [[maybe_unused]] const std::string& msg) { /* TODO notifications */ },
          [this](int32_t retVal, const std::string& lastError) {
              std::map<std::string, std::string> data {
                  {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/message", lastError},
                  {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status", retVal == 0 ? "succeeded" : "failed"},
              };

              utils::valuesPush(data, std::make_shared<::sysrepo::Session>(m_srConn, SR_DS_OPERATIONAL));
          }))
    , m_log(spdlog::get("system"))
{
    {
        auto raucOperation = m_rauc->operation();
        auto raucLastError = m_rauc->lastError();
        std::map<std::string, std::string> data {
            {CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/message", raucLastError},
        };

        if (raucOperation == "installing") {
            data[CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status"] = "in-progress";
        } else if (!raucLastError.empty()) {
            data[CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status"] = "failed";
        } else {
            data[CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/status"] = "none";
        }

        utils::valuesPush(data, m_srSession, SR_DS_OPERATIONAL);
    }

    m_srSubscribe->rpc_subscribe(
        (CZECHLIGHT_SYSTEM_FIRMWARE_MODULE_PREFIX + "installation/install").c_str(),
        [this](::sysrepo::S_Session session, [[maybe_unused]] const char* op_path, const ::sysrepo::S_Vals input, [[maybe_unused]] sr_event_t event, [[maybe_unused]] uint32_t request_id, [[maybe_unused]] ::sysrepo::S_Vals_Holder output) {
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
}
}
