/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Subscription.hpp>
#include "sysrepo-helpers/common.h"
#include "test_log_setup.h"

/** @brief Watch for a given RPC */
struct RPCWatcher {
    RPCWatcher(sysrepo::Session& session, const std::string& xpath);
    MAKE_MOCK1(rpc, void(const Values&));

private:
    sysrepo::Subscription m_sub;
};

#define REQUIRE_RPC_CALL(WATCHER, VALUES) REQUIRE_CALL(WATCHER, rpc(VALUES))
