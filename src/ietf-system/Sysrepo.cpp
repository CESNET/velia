/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include "RAUC.h"
#include "Sysrepo.h"
#include "utils/log.h"

using namespace std::literals;

namespace {

const auto IETF_SYSTEM_MODULE_NAME = "ietf-system"s;
const auto IETF_SYSTEM_STATE_MODULE_PREFIX = "/"s + IETF_SYSTEM_MODULE_NAME + ":system-state/"s;

}

namespace velia::ietf_system::sysrepo {

Sysrepo::Sysrepo(std::shared_ptr<::sysrepo::Session> srSession, std::shared_ptr<RAUC> rauc)
    : m_srSession(std::move(srSession))
    , m_log(spdlog::get("system"))
{
    // ietf-system:system-state with data from RAUC
    // see RFC7317 and https://man7.org/linux/man-pages/man2/uname.2.html
    std::string raucPrimarySlot = rauc->primarySlot();
    auto raucPrimarySlotStatus = rauc->slotStatus().at(raucPrimarySlot);

    std::map<std::string, std::string> opsSystemStateData {
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-name", "CzechLight"},
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-release", std::get<std::string>(raucPrimarySlotStatus.at("bundle.version"))},
    };

    sr_datastore_t oldDatastore = m_srSession->session_get_ds();
    m_srSession->session_switch_ds(SR_DS_OPERATIONAL);

    for (const auto& [k, v] : opsSystemStateData) {
        m_log->debug("Pushing to sysrepo: {} = {}", k, v);
        m_srSession->set_item_str(k.c_str(), v.c_str());
    }

    m_srSession->apply_changes();
    m_srSession->session_switch_ds(oldDatastore);
}
}
