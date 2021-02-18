/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include "Sysrepo.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace velia::ietf_hardware::sysrepo {

namespace {

const auto IETF_HARDWARE_MODULE_NAME = "ietf-hardware"s;
const auto IETF_HARDWARE_MODULE_PREFIX = "/"s + IETF_HARDWARE_MODULE_NAME + ":hardware/*"s;

}

/** @brief The constructor expects the HardwareState instance which will provide the actual hardware state data */
Sysrepo::Sysrepo(std::shared_ptr<::sysrepo::Subscribe> srSubscribe, std::shared_ptr<IETFHardware> hwState)
    : m_hwState(std::move(hwState))
    , m_srSubscribe(std::move(srSubscribe))
    , m_srLastRequestId(0)
{
    m_srSubscribe->oper_get_items_subscribe(
        IETF_HARDWARE_MODULE_NAME.c_str(),
        [this](std::shared_ptr<::sysrepo::Session> session, [[maybe_unused]] const char* module_name, [[maybe_unused]] const char* xpath, [[maybe_unused]] const char* request_xpath, [[maybe_unused]] uint32_t request_id, std::shared_ptr<libyang::Data_Node>& parent) {
            auto hwStateValues = m_hwState->process();
            utils::valuesToYang(hwStateValues, session, parent);

            spdlog::get("main")->trace("Pushing to sysrepo (JSON): {}", parent->print_mem(LYD_FORMAT::LYD_JSON, 0));

            return SR_ERR_OK;
        },
        IETF_HARDWARE_MODULE_PREFIX.c_str(),
        SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
}
}
