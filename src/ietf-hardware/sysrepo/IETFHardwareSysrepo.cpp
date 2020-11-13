/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include "IETFHardwareSysrepo.h"
#include "utils/log.h"

namespace velia::ietf_hardware::sysrepo {

/** @class OpsCallback
 *  A callback class for operational data in Sysrepo. This class expects a shared_pointer<HardwareState> instance.
 *  When sysrepo callbacks for the data in the subtree this callback is registered for, it asks HardwareState instace
 *  for the data it should return back to Sysrepo.
 *  OpsCallback then creates the YANG tree structure from the data returned by HardwareState and returns it.
 *
 *  @see HardwareState
 */

namespace {

const auto IETF_HARDWARE_MODULE_PREFIX = "/ietf-hardware-state:hardware"s;
const auto IETF_HARDWARE_MODULE_NAME = "ietf-hardware-state"s;

void valuesToYang(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent)
{
    for (const auto& [propertyName, value] : values) {
        std::string valuePath = propertyName;

        spdlog::get("main")->debug("propertyName: {}, value: {}", propertyName, value.c_str());
        spdlog::get("main")->trace("  processing {}", valuePath);

        if (!parent) {
            parent = std::make_shared<libyang::Data_Node>(
                session->get_context(),
                valuePath.c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        } else {
            parent->new_path(
                session->get_context(),
                valuePath.c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        }
    }
}
}

/** @brief The constructor expects the HardwareState instance which will provide the actual hardware state data */
IETFHardwareSysrepo::IETFHardwareSysrepo(std::shared_ptr<::sysrepo::Subscribe> srSubscribe, std::shared_ptr<IETFHardware> hwState)
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
