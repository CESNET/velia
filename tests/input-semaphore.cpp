/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <chrono>
#include <thread>
#include "dbus-helpers/dbus_semaphore_server.h"
#include "inputs/DbusSemaphoreInput.h"
#include "manager/AbstractManager.h"
#include "test_log_setup.h"

class FakeManager : public cla::AbstractManager {
public:
    MAKE_MOCK2(updateState, void(void*, State), override);
    MAKE_MOCK2(registerInput, void(void*, State), override);
    MAKE_MOCK1(unregisterInput, void(void*), override);
    MAKE_MOCK1(registerOutput, void(std::shared_ptr<cla::AbstractOutput>), override);
    MAKE_MOCK1(unregisterOutput, void(std::shared_ptr<cla::AbstractOutput>), override);
};

TEST_CASE("Test semaphore input")
{
    using namespace std::literals::chrono_literals;

    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    const std::string dbusBus = "cz.cesnet.led";
    const std::string dbusObj = "/cz/cesnet/led";
    const std::string dbusProp = "Semaphore";
    const std::string dbusPropIface = "cz.cesnet.Led";

    std::vector<std::pair<std::string, std::chrono::milliseconds>> stateSequence;
    /*
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
    }*/
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

    // FIXME: So far systemBus until DbusConnectionInput rewritten to take a dbus connection as a parameter.
    auto conn = sdbus::createSystemBusConnection();
    auto mx = std::make_shared<FakeManager>();

    // i1 should register itself
    REQUIRE_CALL(*mx, registerInput(ANY(void*), State::OK)).IN_SEQUENCE(seq1);
    // REQUIRE_CALL(*mx, updateState(ANY(void*), State::OK))IN_SEQUENCE(seq1); // in reality, this would be called

    std::shared_ptr<cla::AbstractInput> i1 = std::make_shared<cla::DbusSemaphoreInput>(mx, *conn, dbusBus, dbusObj, dbusProp, dbusPropIface);
    // i1 now listens for dbus events, we can start semaphore server

    // mux should get notified for every semaphore state change
    REQUIRE_CALL(*mx, updateState(i1.get(), State::OK)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), State::OK)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), State::WARNING)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), State::ERROR)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), State::WARNING)).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*mx, updateState(i1.get(), State::OK)).IN_SEQUENCE(seq1);

    std::thread thr([&conn]() {
        conn->enterEventLoop();
    });

    std::thread serverThread([&] {
        DbusSemaphoreServer sem(dbusBus, dbusObj, dbusProp, dbusPropIface);
        sem.run(stateSequence);
    });

    serverThread.join();
    conn->leaveEventLoop();
    thr.join();

    REQUIRE_CALL(*mx, unregisterInput(i1.get())).IN_SEQUENCE(seq1);
    i1.reset();
}
