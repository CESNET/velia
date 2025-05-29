/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include "LLDPSysrepo.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

namespace velia::network {

LLDPSysrepo::LLDPSysrepo(sysrepo::Session& session, std::shared_ptr<LLDPDataProvider> lldp)
    : m_log(spdlog::get("network"))
    , m_lldp(std::move(lldp))
    , m_sub(session.onOperGet(
          "czechlight-lldp",
          [this](sysrepo::Session session, auto, auto, auto, auto, auto, std::optional<libyang::DataNode>& output) {
              fetch(session, output);
              return sysrepo::ErrorCode::Ok;
          },
          "/czechlight-lldp:nbr-list"))
{
    utils::ScopedDatastoreSwitch sw(session, sysrepo::Datastore::Operational);
    for (const auto& [key, value] : m_lldp->localProperties()) {
        session.setItem("/czechlight-lldp:local/" + key, value);
    }
    session.applyChanges();
}

void LLDPSysrepo::fetch(sysrepo::Session session, std::optional<libyang::DataNode>& output)
{
    output = session.getContext().newPath("/czechlight-lldp:nbr-list");

    for (const auto& n : m_lldp->getNeighbors()) {
        auto ifc = output->newPath("neighbors");

        auto ifName = ifc->newPath("ifName", n.m_portId);

        for (const auto& [key, val] : n.m_properties) { // garbage properties in, garbage out
            ifc->newPath(key, val);
        }
    }

    m_log->trace("Pushing to sysrepo (JSON): {}", *output->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
}

}
