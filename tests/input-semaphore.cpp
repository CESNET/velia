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
#include "manager/StateManager.h"
#include "test_log_setup.h"

class FakeManager : public cla::StateManager {
public:
    MAKE_MOCK0(notifyInputChanged, void());
};

TEST_CASE("Test semaphore input")
{
    using namespace std::literals::chrono_literals;

    TEST_INIT_LOGS;

    const std::string dbusBus = "cz.cesnet.led";
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
            {"OK", 0ms},
        };
    }
    SECTION("Sequence without pauses")
    {
        stateSequence = {
            {"ERROR", 0ms},
            {"WARNING", 0ms},
            {"OK", 0ms},
            {"ERROR", 0ms},
            {"WARNING", 0ms},
            {"OK", 0ms},
        };
    }

    auto conn = sdbus::createSystemBusConnection(); // order matters, conn must destroy *after* destroying i1. i1 destroys with mx.
    FakeManager mx;
    auto i1 = std::make_shared<cla::DbusSemaphoreInput>(*conn, dbusBus, dbusObj, dbusProp, dbusPropIface);

    /* FIXME probably need an interface?, this does not mock
    trompeloeil::sequence seq1;
    for (const auto state: stateSequence) {
        REQUIRE_CALL(mx, notifyInputChanged()).IN_SEQUENCE(seq1);
    }
    */

    i1->connectToManager(mx);

    std::thread thr([&conn]() {
        conn->enterEventLoop();
    });
    // i1 now listens for dbus events, we can start semaphore server

    std::thread serverThread([&] {
        DbusSemaphoreServer sem(dbusBus, dbusObj, dbusProp, dbusPropIface);
        sem.run(stateSequence);
    });

    /* assert vsecky stavy */

    serverThread.join();
    conn->leaveEventLoop();
    thr.join();
}
