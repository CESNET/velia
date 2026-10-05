/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <condition_variable>
#include <mutex>
#include "LLDPSysrepo.h"
#include "utils/log.h"

namespace velia::network {

LLDPSysrepo::LLDPSysrepo(sysrepo::Connection conn, std::shared_ptr<LLDPDataProvider> lldp, std::chrono::milliseconds pollInterval)
    : m_log(spdlog::get("network"))
    , m_session(conn.sessionStart(sysrepo::Datastore::Operational))
    , m_lldp(std::move(lldp))
    , m_pollInterval(pollInterval)
    , m_lastNeighbors(m_lldp->getNeighbors())
{
    push(m_lastNeighbors);

    m_thread = std::jthread([this](std::stop_token stop) {
        // nobody else uses these; the condition variable only wakes us up early when a stop is requested
        std::mutex mtx;
        std::condition_variable_any cv;
        std::unique_lock lock(mtx);

        while (!cv.wait_for(lock, stop, m_pollInterval, [&stop] { return stop.stop_requested(); })) {
            poll();
        }
    });
}

void LLDPSysrepo::poll()
{
    try {
        auto neighbors = m_lldp->getNeighbors();
        if (neighbors == m_lastNeighbors) {
            return;
        }

        push(neighbors);
        m_lastNeighbors = std::move(neighbors);
    } catch (const std::exception& e) {
        m_log->error("Failed to update LLDP neighbors: {}", e.what());
    }
}

void LLDPSysrepo::push(const std::vector<NeighborEntry>& neighbors)
{
    auto edit = m_session.getContext().newPath("/czechlight-lldp:nbr-list");

    for (const auto& n : neighbors) {
        auto ifc = edit.newPath("neighbors");

        ifc->newPath("ifName", n.m_portId);

        for (const auto& [key, val] : n.m_properties) { // garbage properties in, garbage out
            ifc->newPath(key, val);
        }
    }

    for (const auto& [key, value] : m_lldp->localProperties()) {
        edit.newPath("/czechlight-lldp:local/" + key, value);
    }

    // The tree contains everything this session pushes, so a replace drops the neighbors which are gone
    auto data = edit.firstSibling();
    m_log->trace("Pushing to sysrepo (JSON): {}", *data.printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Siblings));
    m_session.editBatch(data, sysrepo::DefaultOperation::Replace);
    m_session.applyChanges();
}

}
