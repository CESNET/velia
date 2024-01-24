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

/** @brief Watch for datastore changes */
struct DatastoreWatcher {
    DatastoreWatcher(sysrepo::Session& session, const std::string& xpath, const std::set<std::string>& ignoredPaths = {});
    MAKE_MOCK1(change, void(const ValueChanges&));

private:
    std::set<std::string> m_ignoredPaths;
    sysrepo::Subscription m_sub;
};

#define REQUIRE_DATASTORE_CHANGE(WATCHER, CHANGES) REQUIRE_CALL(WATCHER, change(CHANGES))
