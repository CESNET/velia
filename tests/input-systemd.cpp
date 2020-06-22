/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "inputs/DbusSystemdInput.h"
#include "test_log_setup.h"

TEST_CASE("Test systemd input")
{
    TEST_INIT_LOGS;

    auto clientConnection = sdbus::createSystemBusConnection();

    auto mx = std::make_shared<velia::StateManager>();
    auto i1 = velia::DbusSystemdInput(mx, *clientConnection);

    clientConnection->enterEventLoop();
}
