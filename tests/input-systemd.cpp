/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_systemd_server.h"
#include "fake.h"
#include "inputs/DbusSystemdInput.h"
#include "test_log_setup.h"
#include "utils/log-init.h"
#include "utils/log.h"

TEST_CASE("Systemd monitor")
{
    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    // setup separate connections for both client and server. Can be done using one connection only but this way it is more generic
    // enter client and servers event loops
    auto clientConnection = sdbus::createSessionBusConnection();
    auto serverConnection = sdbus::createSessionBusConnection("cz.cesnet.systemd1");
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    auto mx = std::make_shared<FakeManager>();
    auto server = DbusSystemdServer(*serverConnection);

    // i1 gets constructed which means:
    //  - a registration is performed, along with an updateState call (State::OK)
    //  - i1's constructor queries the current state and performs updateState
    REQUIRE_CALL(*mx, registerInput(ANY(void*), velia::State::OK)).LR_SIDE_EFFECT(mx->updateState(_1, _2)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::OK)).IN_SEQUENCE(seq1);

    // create units
    server.createUnit(*serverConnection, "/cz/cesnet/systemd1/unit/unit1", "active", "running");
    server.createUnit(*serverConnection, "/cz/cesnet/systemd1/unit/unit2", "activating", "auto-restart");
    server.createUnit(*serverConnection, "/cz/cesnet/systemd1/unit/unit3", "failed", "failed");

    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::OK)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::ERROR)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::ERROR)).IN_SEQUENCE(seq1);
    auto i1 = std::make_shared<velia::DbusSystemdInput>(mx, *clientConnection, "cz.cesnet.systemd1", "/cz/cesnet/systemd1", "cz.cesnet.systemd1.Manager", "cz.cesnet.systemd1.Unit");
    // i1 now listens for dbus events, we can start the semaphore server

    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::ERROR)).IN_SEQUENCE(seq1);
    server.changeUnitState("/cz/cesnet/systemd1/unit/unit2", "active", "running");

    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::OK)).IN_SEQUENCE(seq1);
    server.changeUnitState("/cz/cesnet/systemd1/unit/unit3", "active", "running");

    // all units OK now

    // add new unit with failed/failed, DbusSystemdInput should receive UnitNew signal and monitor this unit too
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::ERROR)).IN_SEQUENCE(seq1);
    server.createUnit(*serverConnection, "/cz/cesnet/systemd1/unit/unit4", "failed", "failed");

    waitForCompletionAndBitMore(seq1);

    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::OK)).IN_SEQUENCE(seq1);
    server.changeUnitState("/cz/cesnet/systemd1/unit/unit4", "active", "running");

    waitForCompletionAndBitMore(seq1);

    REQUIRE_CALL(*mx, unregisterInput(i1.get())).IN_SEQUENCE(seq1);
    i1.reset();
}

#if 0
TEST_CASE("Real systemd monitor")
{
    TEST_INIT_LOGS;

    auto clientConnection = sdbus::createSystemBusConnection();

    auto mx = std::make_shared<velia::StateManager>();
    auto i1 = velia::DbusSystemdInput(mx, *clientConnection);

    clientConnection->enterEventLoop();
}
#endif