/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/mock/system.h"

#define REAL_SYSTEM_TEST
#ifdef REAL_SYSTEM_TEST
TEST_CASE("ietf-interfaces on live system")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess, std::make_shared<velia::system::Rtnetlink>());

    for (const auto& [k, v] : dataFromSysrepo(client, "/ietf-interfaces:interfaces", SR_DS_OPERATIONAL)) {
        spdlog::get("main")->info("{}: {}", k, v);
    }
}
#endif
