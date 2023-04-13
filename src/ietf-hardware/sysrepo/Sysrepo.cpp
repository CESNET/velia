/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <chrono>
#include "Sysrepo.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace velia::ietf_hardware::sysrepo {

namespace {
using namespace std::chrono_literals;
const auto POLL_PERIOD = 1500ms;
}

/** @brief The constructor expects the HardwareState instance which will provide the actual hardware state data */
Sysrepo::Sysrepo(::sysrepo::Session session, std::shared_ptr<IETFHardware> hwState)
    : m_Log(spdlog::get("hardware"))
    , m_session(std::move(session))
    , m_hwState(std::move(hwState))
    , m_quit(false)
    , m_pollThread([&]() {
        m_Log->trace("Poll thread started");

        while (!m_quit) {
            m_Log->trace("IetfHardware poll");

            auto hwStateValues = m_hwState->process();
            utils::valuesPush(hwStateValues, {}, m_session, ::sysrepo::Datastore::Operational);

            std::this_thread::sleep_for(POLL_PERIOD);
        }
        m_Log->trace("Poll thread stopped");
    })
{
}

Sysrepo::~Sysrepo()
{
    m_Log->trace("Requesting poll thread stop");
    m_quit = true;
    m_pollThread.join();
}
}
