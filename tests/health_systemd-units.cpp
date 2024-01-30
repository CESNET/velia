/*
 * Copyright (C) 2020 - 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/Subscription.hpp>
#include <thread>
#include "dbus-helpers/dbus_systemd_server.h"
#include "health/SystemdUnits.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/sysrepo-helpers/alarms.h"
#include "tests/sysrepo-helpers/common.h"
#include "utils/log-init.h"
#include "utils/log.h"

using namespace std::chrono_literals;

#define REQUIRE_NEW_ALARM_INVENTORY_UNIT(UNIT) REQUIRE_NEW_ALARM_INVENTORY_RESOURCE(alarmsWatcher, "velia-alarms:systemd-unit-failure", "", std::set<std::string>{UNIT})
#define REQUIRE_ALARM_RPC(UNIT, SEVERITY, TEXT) REQUIRE_NEW_ALARM(alarmsWatcher, "velia-alarms:systemd-unit-failure", "", UNIT, SEVERITY, TEXT)

TEST_CASE("systemd unit state monitoring (alarms)")
{
    TEST_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    trompeloeil::sequence seq1;

    // Create and setup separate connections for both client and server to simulate real-world server-client architecture.
    // Also this doesn't work one a single dbus connection.
    auto clientConnection = sdbus::createSessionBusConnection();
    auto serverConnection = sdbus::createSessionBusConnection();
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    auto server = DbusSystemdServer(*serverConnection);

    client.switchDatastore(sysrepo::Datastore::Operational);
    AlarmWatcher alarmsWatcher(client);

    REQUIRE_NEW_ALARM_INVENTORY_ENTRY(alarmsWatcher,
                                      "velia-alarms:systemd-unit-failure",
                                      "",
                                      (std::set<std::string>{"unit1.service", "unit2.service", "unit3.service"}),
                                      (std::set<std::string>{"critical"}),
                                      true,
                                      "The systemd service is considered in failed state.");

    REQUIRE_ALARM_RPC("unit1.service", "cleared", "systemd unit state: (active, running)");
    server.createUnit(*serverConnection, "unit1.service", "/org/freedesktop/systemd1/unit/unit1", "active", "running");

    REQUIRE_ALARM_RPC("unit2.service", "critical", "systemd unit state: (activating, auto-restart)");
    server.createUnit(*serverConnection, "unit2.service", "/org/freedesktop/systemd1/unit/unit2", "activating", "auto-restart");

    REQUIRE_ALARM_RPC("unit3.service", "critical", "systemd unit state: (failed, failed)");
    server.createUnit(*serverConnection, "unit3.service", "/org/freedesktop/systemd1/unit/unit3", "failed", "failed");

    auto systemdAlarms = std::make_shared<velia::health::SystemdUnits>(srSess, *clientConnection, serverConnection->getUniqueName(), "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit");

    REQUIRE_ALARM_RPC("unit2.service", "cleared", "systemd unit state: (active, running)");
    REQUIRE_ALARM_RPC("unit3.service", "cleared", "systemd unit state: (active, running)");
    REQUIRE_NEW_ALARM_INVENTORY_UNIT("unit4.service");
    REQUIRE_ALARM_RPC("unit4.service", "critical", "systemd unit state: (failed, failed)");
    REQUIRE_ALARM_RPC("unit3.service", "critical", "systemd unit state: (activating, auto-restart)");
    REQUIRE_ALARM_RPC("unit3.service", "cleared", "systemd unit state: (active, running)");
    REQUIRE_ALARM_RPC("unit3.service", "critical", "systemd unit state: (failed, failed)");
    REQUIRE_ALARM_RPC("unit3.service", "critical", "systemd unit state: (activating, auto-restart)");
    REQUIRE_ALARM_RPC("unit3.service", "cleared", "systemd unit state: (active, running)");
    REQUIRE_ALARM_RPC("unit4.service", "cleared", "systemd unit state: (active, running)");

    std::thread systemdSimulator([&] {
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit2", "active", "running");
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");

        // In case we obtain a notifications that unit changed state from (X,Y) to (X,Y), do not trigger any events.
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");

        // add new unit with failed/failed, DbusSystemdInput should receive UnitNew signal and monitor this unit too
        server.createUnit(*serverConnection, "unit4.service", "/org/freedesktop/systemd1/unit/unit4", "failed", "failed");

        // Sleep for a while; the rest of the code might be too fast and we need to be sure that we pick up event for (failed, failed) before the state of unit4 is changed in the DBus server
        std::this_thread::sleep_for(250ms);

        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "activating", "auto-restart");
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "failed", "failed");
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "activating", "auto-restart");
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");

        server.changeUnitState("/org/freedesktop/systemd1/unit/unit4", "active", "running");
    });

    systemdSimulator.join();
    waitForCompletionAndBitMore(seq1);
}
