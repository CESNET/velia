/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <chrono>
#include <functional>
#include <future>
#include "dbus-helpers/dbus_semaphore_server.h"
#include "health/inputs/DbusSemaphoreInput.h"
#include "mock/health.h"
#include "test_log_setup.h"

TEST_CASE("Test semaphore input")
{
    using namespace std::literals::chrono_literals;

    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    const std::string dbusObj = "/cz/cesnet/led";
    const std::string dbusProp = "Semaphore";
    const std::string dbusPropIface = "cz.cesnet.Led";

    std::vector<std::pair<std::string, std::chrono::milliseconds>> stateSequence;
    SECTION("Sequence with pauses")
    {
        stateSequence = {
            {"OK", 505ms},
            {"OK", 311ms},
            {"WARNING", 143ms},
            {"ERROR", 87ms},
            {"WARNING", 333ms},
            {"OK", 1ms},
        };
    }
    SECTION("Sequence without pauses")
    {
        stateSequence = {
            {"OK", 0ms},
            {"OK", 0ms},
            {"WARNING", 0ms},
            {"ERROR", 0ms},
            {"WARNING", 0ms},
            {"OK", 0ms},
        };
    }

    // setup separate connections for both client and server. Can be done using one connection only but this way it is more generic
    auto clientConnection = sdbus::createSessionBusConnection();
    auto serverConnection = sdbus::createSessionBusConnection();

    // enter client and servers event loops
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    auto mx = std::make_shared<FakeManager>();
    auto server = DbusSemaphoreServer(*serverConnection, dbusObj, dbusProp, dbusPropIface, "ERROR"); // let the first state be ERROR, because why not

    // i1 gets constructed which means:
    //  - a registration is performed, along with an updateState call (State::OK)
    //  - i1's constructor queries the current state and performs updateState
    REQUIRE_CALL(*mx, registerInput(ANY(void*), velia::health::State::OK)).LR_SIDE_EFFECT(mx->updateState(_1, _2)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::health::State::OK)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(ANY(void*), velia::health::State::ERROR)).IN_SEQUENCE(seq1);
    auto i1 = std::make_shared<velia::health::DbusSemaphoreInput>(mx, *clientConnection, serverConnection->getUniqueName(), dbusObj, dbusProp, dbusPropIface);
    // i1 now listens for dbus events, we can start the semaphore server

    // mux should get notified for every semaphore state change
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::health::State::OK)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::health::State::OK)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::health::State::WARNING)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::health::State::ERROR)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::health::State::WARNING)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), velia::health::State::OK)).IN_SEQUENCE(seq1);


    auto a1 = std::async(std::launch::async, [&]() { server.runStateChanges(stateSequence); });

    waitForCompletionAndBitMore(seq1); // do not leave event loops until all dbus messages are received
    serverConnection->leaveEventLoop();
    clientConnection->leaveEventLoop();

    REQUIRE_CALL(*mx, unregisterInput(i1.get())).IN_SEQUENCE(seq1);
    i1.reset();
}
