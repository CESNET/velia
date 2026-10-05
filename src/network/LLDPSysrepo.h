/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <chrono>
#include <memory>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Session.hpp>
#include <thread>
#include "LLDP.h"
#include "utils/log-fwd.h"

namespace velia::network {

class LLDPSysrepo {
public:
    explicit LLDPSysrepo(sysrepo::Connection conn, std::shared_ptr<LLDPDataProvider> lldp, std::chrono::milliseconds pollInterval);

private:
    void poll();
    void push(const std::vector<NeighborEntry>& neighbors);

    velia::Log m_log;
    sysrepo::Session m_session;
    std::shared_ptr<LLDPDataProvider> m_lldp;
    std::chrono::milliseconds m_pollInterval;
    std::vector<NeighborEntry> m_lastNeighbors;
    std::jthread m_thread; // last member, it must be destroyed first
};

}
