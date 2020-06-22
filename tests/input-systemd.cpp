/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "dbus-helpers/dbus_systemd_server.h"
//#include "trompeloeil_doctest.h"
#include "inputs/DbusSystemdInput.h"

//#include "test_log_setup.h"
#include <iostream>
#include <spdlog/sinks/ansicolor_sink.h>
#include <thread>
#include "utils/log-init.h"
#include "utils/log.h"

//#include "fake.h"

int main()
{
    spdlog::drop_all();
    auto test_logger = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    velia::utils::initLogs(test_logger);
    spdlog::set_pattern("%S.%e [%t %n %L] %v");
    spdlog::set_level(spdlog::level::trace);
    //trompeloeil::stream_tracer tracer {std::cout};
    //trompeloeil::sequence seq1;

    // setup separate connections for both client and server. Can be done using one connection only but this way it is more generic
    auto clientConnection = sdbus::createSessionBusConnection();
    auto serverConnection = sdbus::createSessionBusConnection("cz.cesnet.systemd1");

    // enter client and servers event loops
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    auto mx = std::make_shared<velia::StateManager>();
    auto server = DbusSystemdServer(*serverConnection);

    // i1 gets constructed which means:
    //  - a registration is performed, along with an updateState call (State::OK)
    //  - i1's constructor queries the current state and performs updateState
    //REQUIRE_CALL(*mx, registerInput(ANY(void*), velia::State::OK)).LR_SIDE_EFFECT(mx->updateState(_1, _2)).IN_SEQUENCE(seq1);
    //REQUIRE_CALL(*mx, updateState(ANY(void*), velia::State::OK)).IN_SEQUENCE(seq1);
    auto i1 = std::make_shared<velia::DbusSystemdInput>(mx, *clientConnection, "cz.cesnet.systemd1", "/cz/cesnet/systemd1", "cz.cesnet.systemd1.Manager", "cz.cesnet.systemd1.Unit");
    // i1 now listens for dbus events, we can start the semaphore server

    //REQUIRE_CALL(*mx, unregisterInput(i1.get())).IN_SEQUENCE(seq1);
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