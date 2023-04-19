/*
 * Copyright (C) 2020-2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <sysrepo-cpp/Connection.hpp>
#include "Sysrepo.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace velia::ietf_hardware::sysrepo {

/** @brief The constructor expects the HardwareState instance which will provide the actual hardware state data and the poll interval */
Sysrepo::Sysrepo(::sysrepo::Session session, std::shared_ptr<IETFHardware> hwState, std::chrono::microseconds pollInterval)
    : m_Log(spdlog::get("hardware"))
    , m_pollInterval(std::move(pollInterval))
    , m_session(std::move(session))
    , m_hwState(std::move(hwState))
    , m_quit(false)
    , m_pollThread([&]() {
        m_Log->trace("Poll thread started");
        auto conn = m_session.getConnection();

        while (!m_quit) {
            m_Log->trace("IetfHardware poll");

            HardwareInfo hwInfo = m_hwState->process();

            /* Some data readers can stop returning data in some cases (e.g. ejected PSU).
             * Delete tree before updating to avoid having not current data from previous polls. */
            conn.discardOperationalChanges("/ietf-hardware:hardware");
            utils::valuesPush(hwInfo.dataTree, {}, m_session, ::sysrepo::Datastore::Operational);

            std::this_thread::sleep_for(m_pollInterval);
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
