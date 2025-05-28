/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include "LLDPSysrepo.h"
#include "utils/log.h"

namespace velia::system {

LLDPSysrepo::LLDPSysrepo(std::shared_ptr<LLDPDataProvider> lldp)
    : m_log(spdlog::get("system"))
    , m_lldp(std::move(lldp))
{
}

sysrepo::ErrorCode LLDPSysrepo::operator()(sysrepo::Session session, uint32_t, const std::string&, const std::optional<std::string>& subXPath, const std::optional<std::string>& requestXPath, uint32_t, std::optional<libyang::DataNode>& output)
{
    m_log->trace("operational data callback: subXPath {} request-XPath {}",
            subXPath ? *subXPath : "(none)", requestXPath ? *requestXPath : "(none)");

    output = session.getContext().newPath("/czechlight-lldp:nbr-list");

    for (const auto& n : m_lldp->getNeighbors()) {
        auto ifc = output->newPath("neighbors");

        auto ifName = ifc->newPath("ifName", n.m_portId);

        for (const auto& [key, val] : n.m_properties) { // garbage properties in, garbage out
            ifc->newPath(key, val);
        }
    }

    m_log->trace("Pushing to sysrepo (JSON): {}", *output->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));

    return sysrepo::ErrorCode::Ok;
}

}
