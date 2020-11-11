/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include "OpsCallback.h"

#include <utility>
#include "utils/log.h"

namespace velia::hardware::sysrepo {

namespace impl {

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

OpsCallback::OpsCallback(std::shared_ptr<HardwareState> hwState)
    : m_hwState(std::move(hwState))
    , m_lastRequestId(0)
{
}

int OpsCallback::operator()(std::shared_ptr<::sysrepo::Session> session, const char* module_name, const char* xpath, const char* request_xpath, uint32_t request_id, std::shared_ptr<libyang::Data_Node>& parent)
{
    spdlog::get("main")->debug("operational data callback: XPath {} req {} orig-XPath {}", xpath, request_id, request_xpath);

    // when asking for something in the subtree of THIS request
    if (m_lastRequestId == request_id) {
        spdlog::trace(" ops data request already handled");
        return SR_ERR_OK;
    }
    m_lastRequestId = request_id;

    auto ctx = session->get_context();
    auto mod = ctx->get_module(module_name);

    auto hwStateValues = m_hwState->process();
    impl::valuesToYang(hwStateValues, session, parent);

    spdlog::get("main")->trace("Pushing to sysrepo (JSON): {}", parent->print_mem(LYD_FORMAT::LYD_JSON, 0));

    return SR_ERR_OK;
}
}
