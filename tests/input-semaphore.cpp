/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <thread>
#include "dbus-semaphore_server.h"
#include "inputs/DbusSemaphoreInput.h"
#include "manager/StateManager.h"
#include "test_log_setup.h"

class FakeManager : public cla::StateManager {
public:
    MAKE_MOCK0(notifyInputChanged, void());
};

TEST_CASE("Test semaphore input")
{
    TEST_INIT_LOGS;

    std::vector<std::string> stateSequence {"OK", "ERROR", "WARNING", "OK", "ERROR", "WARNING", "OK", "ERROR", "WARNING", "OK", "ERROR", "WARNING", "OK", "ERROR", "WARNING"};

    auto conn = sdbus::createSystemBusConnection(); // order matters, conn must destroy *after* destroying i1. i1 destroys with mx.
    FakeManager mx;
    auto i1 = std::make_shared<cla::DbusSemaphoreInput>(*conn, "cz.cesnet.led", "/cz/cesnet/led", "Semaphore", "cz.cesnet.Led");

    /* FIXME probably need an itnerface, this does not mock
    trompeloeil::sequence seq1;
    for (const auto state: stateSequence) {
        REQUIRE_CALL(mx, notifyInputChanged()).IN_SEQUENCE(seq1);
    }
    */

    i1->connectToManager(mx);

    std::thread thr([&conn, &mx, &stateSequence]() {
        conn->enterEventLoop();
    });

    // i1 now listens, we can start server

    std::thread serverThread([&stateSequence] {
        DbusSemaphoreServer sem("cz.cesnet.led", "/cz/cesnet/led", "cz.cesnet.Led");
        sem.run(stateSequence);
    });

    /* assert vsecky stavy */

    serverThread.join();
    conn->leaveEventLoop();
    thr.join();
}
