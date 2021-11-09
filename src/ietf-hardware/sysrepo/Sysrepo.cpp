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
Sysrepo::Sysrepo(::sysrepo::Session session, std::shared_ptr<IETFHardware> hwState)
    : m_hwState(std::move(hwState))
    , m_srSubscribe()
{
    ::sysrepo::OperGetCb cb = [this](::sysrepo::Session session, auto, auto, auto, auto, auto, auto& parent) {
        auto hwStateValues = m_hwState->process();
        utils::valuesToYang(hwStateValues, {}, session, parent);

        spdlog::get("hardware")->trace("Pushing to sysrepo (JSON): {}", parent->printStr(::libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings)->get().get());
        return ::sysrepo::ErrorCode::Ok;
    };

    m_srSubscribe = session.onOperGet(
        IETF_HARDWARE_MODULE_NAME.c_str(),
        cb,
        IETF_HARDWARE_MODULE_PREFIX.c_str(),
        ::sysrepo::SubscribeOptions::Passive | ::sysrepo::SubscribeOptions::OperMerge);
}
}
