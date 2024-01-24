/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "rpc.h"
#include "sysrepo-helpers/common.h"

RPCWatcher::RPCWatcher(sysrepo::Session& session, const std::string& xpath)
    : m_sub(session.onRPCAction(xpath, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, auto) {
        std::map<std::string, std::string> in;

        for (auto n : input.childrenDfs()) {
            in.emplace(n.path(), nodeAsString(n));
        }

        rpc(in);
        return sysrepo::ErrorCode::Ok;
    }))
{
}
