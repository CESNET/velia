/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#pragma once
#include "trompeloeil_doctest.h"
#include <map>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include "test_log_setup.h"

using Values = std::map<std::string, std::string>;

Values dataFromSysrepo(const sysrepo::Session& session, const std::string& xpath);
Values rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, std::map<std::string, std::string> input);
Values dataFromSysrepo(sysrepo::Session session, const std::string& xpath, sysrepo::Datastore datastore);

#define TEST_SYSREPO_INIT                \
    auto srConn = sysrepo::Connection{}; \
    auto srSess = srConn.sessionStart();

#define TEST_SYSREPO_INIT_CLIENT             \
    auto clientConn = sysrepo::Connection{}; \
    auto client = clientConn.sessionStart();
