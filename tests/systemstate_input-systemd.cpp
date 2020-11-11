/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_systemd_server.h"
#include "inputs/DbusSystemdInput.h"
#include "mock/systemstate.h"
#include "test_log_setup.h"
#include "utils/log-init.h"
#include "utils/log.h"

TEST_CASE("Systemd monitor")
{
    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    // Create and setuo separate connections for both client and server. Could be done using a single connection but this way it is more generic
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

    // create units. Unit2 and Unit3 are in states that we consider failed
    // therefore the DbusSystemdInput will report ERROR after loading the second unit
    // FailedUnits: {unit2, unit3} -> ERROR
    server.createUnit(*serverConnection, "unit1.service", "/cz/cesnet/systemd1/unit/unit1", "active", "running");
    server.createUnit(*serverConnection, "unit2.service", "/cz/cesnet/systemd1/unit/unit2", "activating", "auto-restart");
    server.createUnit(*serverConnection, "unit3.service", "/cz/cesnet/systemd1/unit/unit3", "failed", "failed");
    server.createUnit(*serverConnection, "unitIgnored.service", "/cz/cesnet/systemd1/unit/unitIgnored", "failed", "failed");

    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::OK)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::ERROR)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::ERROR)).IN_SEQUENCE(seq1);
    auto i1 = std::make_shared<velia::DbusSystemdInput>(mx, std::set<std::string> {"unitIgnored.service"}, *clientConnection, "cz.cesnet.systemd1", "/cz/cesnet/systemd1", "cz.cesnet.systemd1.Manager", "cz.cesnet.systemd1.Unit");
    // i1 now listens for dbus events, we can start the semaphore server

    // FailedUnits: {unit3} -> ERROR
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::ERROR)).IN_SEQUENCE(seq1);
    server.changeUnitState("/cz/cesnet/systemd1/unit/unit2", "active", "running");

    // FailedUnits: {} -> OK
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::OK)).IN_SEQUENCE(seq1);
    server.changeUnitState("/cz/cesnet/systemd1/unit/unit3", "active", "running");

    // In case we obtain a notifications that unit changed state from (X,Y) to (X,Y), do not trigger any events.
    server.changeUnitState("/cz/cesnet/systemd1/unit/unit3", "active", "running");

    // add new unit with failed/failed, DbusSystemdInput should receive UnitNew signal and monitor this unit too
    // FailedUnits: {unit4} -> OK
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::ERROR)).IN_SEQUENCE(seq1);
    server.createUnit(*serverConnection, "unit4.service", "/cz/cesnet/systemd1/unit/unit4", "failed", "failed");

    // unitIgnored is ignored by us, so it can change in any way but since we don't obtain the notifications, nothing will happen
    server.changeUnitState("/cz/cesnet/systemd1/unit/unitIgnored", "failed", "failed");
    server.changeUnitState("/cz/cesnet/systemd1/unit/unitIgnored", "active", "auto-restarting");
    server.changeUnitState("/cz/cesnet/systemd1/unit/unitIgnored", "active", "running");

    waitForCompletionAndBitMore(seq1);

    // FailedUnits: {} -> OK
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::State::OK)).IN_SEQUENCE(seq1);
    server.changeUnitState("/cz/cesnet/systemd1/unit/unit4", "active", "running");

    waitForCompletionAndBitMore(seq1);

    REQUIRE_CALL(*mx, unregisterInput(i1.get())).IN_SEQUENCE(seq1);
    i1.reset();
}

#if 0
// Runs a StateManager with DbusSystemdInput connected to the development machine's systemd. Might be useful for debugging.
TEST_CASE("This machine's systemd monitor")
{
    TEST_INIT_LOGS;

    auto clientConnection = sdbus::createSystemBusConnection();

    auto mx = std::make_shared<velia::StateManager>();
    auto i1 = std::make_shared<velia::DbusSystemdInput>(mx, *clientConnection);

    clientConnection->enterEventLoop();
}
#endif
