/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 */
#include <unistd.h>
#include "Hostname.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace {
const auto czechlightSystem = "/czechlight-system:hostname/hostname";
const auto hostnamePath = "/czechlight-system:hostname/hostname";
}

velia::system::Hostname::Hostname(std::shared_ptr<::sysrepo::Session> srSess)
    : m_log(spdlog::get("system"))
    , m_srSession(srSess)
    , m_srSubscribe(std::make_shared<sysrepo::Subscribe>(m_srSession))
{
    m_log->debug("Initializing hostname");
    utils::ensureModuleImplemented(m_srSession, czechlightSystem, "2021-01-13");

    sysrepo::ModuleChangeCb cb = [] (
            sysrepo::S_Session session,
            [[maybe_unused]] const char *module_name,
            [[maybe_unused]] const char *xpath,
            [[maybe_unused]] sr_event_t event,
            [[maybe_unused]] uint32_t request_id) {

        auto data = session->get_changes_iter(hostnamePath);


        while (auto change = session->get_change_tree_next(data)) {
            auto node = change->node();
            if (node->path() == czechlightSystem) {
                auto leaf = std::make_shared<libyang::Data_Node_Leaf_List>(node);
                std::string_view value = leaf->value_str();
                // TODO: how to test?
                sethostname(value.data(), value.size());
            } else {
                throw std::runtime_error("Unknown XPath" + node->path());
            }

        }

        return SR_ERR_OK;
    };

    m_srSubscribe->module_change_subscribe(czechlightSystem, cb, hostnamePath, 0, SR_SUBSCR_DONE_ONLY | SR_SUBSCR_ENABLED);
}
