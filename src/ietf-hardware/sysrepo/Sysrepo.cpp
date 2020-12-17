/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include "Sysrepo.h"
#include "utils/log.h"

namespace velia::ietf_hardware::sysrepo {

namespace {

const auto IETF_HARDWARE_MODULE_NAME = "ietf-hardware-state"s;
const auto IETF_HARDWARE_MODULE_PREFIX = "/"s + IETF_HARDWARE_MODULE_NAME + ":hardware"s;

void valuesToYang(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent)
{
    for (const auto& [propertyName, value] : values) {
        spdlog::get("main")->trace("propertyName: {}, value: {}", propertyName, value.c_str());

        if (!parent) {
            parent = std::make_shared<libyang::Data_Node>(
                session->get_context(),
                propertyName.c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        } else {
            parent->new_path(
                session->get_context(),
                propertyName.c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        }
    }
}
}

/** @brief The constructor expects the HardwareState instance which will provide the actual hardware state data */
Sysrepo::Sysrepo(std::shared_ptr<::sysrepo::Subscribe> srSubscribe, std::shared_ptr<IETFHardware> hwState)
    : m_hwState(std::move(hwState))
    , m_srSubscribe(std::move(srSubscribe))
    , m_srLastRequestId(0)
{
    m_srSubscribe->oper_get_items_subscribe(
        IETF_HARDWARE_MODULE_NAME.c_str(),
        [this](std::shared_ptr<::sysrepo::Session> session, const char* module_name, const char* xpath, const char* request_xpath, uint32_t request_id, std::shared_ptr<libyang::Data_Node>& parent) {
            spdlog::get("main")->debug("operational data callback: XPath {} req {} orig-XPath {}", xpath, request_id, request_xpath);

            // when asking for something in the subtree of THIS request
            if (m_srLastRequestId == request_id) {
                spdlog::trace(" ops data request already handled");
                return SR_ERR_OK;
            }

            m_srLastRequestId = request_id;

            auto ctx = session->get_context();
            auto mod = ctx->get_module(module_name);

            auto hwStateValues = m_hwState->process();
            valuesToYang(hwStateValues, session, parent);

            spdlog::get("main")->trace("Pushing to sysrepo (JSON): {}", parent->print_mem(LYD_FORMAT::LYD_JSON, 0));

            return SR_ERR_OK;
        },
        IETF_HARDWARE_MODULE_PREFIX.c_str(),
        SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
}
}
