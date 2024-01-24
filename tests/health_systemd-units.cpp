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
#include "tests/sysrepo-helpers/datastore.h"
#include "tests/sysrepo-helpers/rpc.h"
#include "utils/log-init.h"
#include "utils/log.h"

using namespace std::chrono_literals;

// clang-format off
#define REQUIRE_ALARM_RPC(RESOURCE, SEVERITY, TEXT) REQUIRE_RPC_CALL(alarmRPC, (Values{                          \
            {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},                                   \
            {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", TEXT},                                   \
            {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:systemd-unit-failure"}, \
            {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},                           \
            {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", RESOURCE},                                 \
            {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", SEVERITY}                                  \
        })).IN_SEQUENCE(seq1).ALARM_INVENTORY_CONTAINS(alarmInventory, "velia-alarms:systemd-unit-failure", "", RESOURCE, SEVERITY)
// clang-format on

#define ALARM_INVENTORY_PATH(TYPE, QUAL) "/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" TYPE "'][alarm-type-qualifier='" QUAL "']"
#define REQUIRE_ALARM_INVENTORY_UNIT(UNIT) REQUIRE_DATASTORE_CHANGE(alarmInventoryChanges, (ValueChanges{ \
            {ALARM_INVENTORY_PATH("velia-alarms:systemd-unit-failure", "") "/resource[1]", UNIT}   \
        })).IN_SEQUENCE(seq1).ALARM_INVENTORY_INSERT(alarmInventory, "velia-alarms:systemd-unit-failure", "", std::set<std::string>{UNIT}, std::set<std::string>{})

TEST_CASE("systemd unit state monitoring (alarms)")
{
    TEST_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    trompeloeil::sequence seq1;

    AlarmInventory alarmInventory;

    // Create and setup separate connections for both client and server to simulate real-world server-client architecture.
    // Also this doesn't work one a single dbus connection.
    auto clientConnection = sdbus::createSessionBusConnection();
    auto serverConnection = sdbus::createSessionBusConnection();
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    auto server = DbusSystemdServer(*serverConnection);

    client.switchDatastore(sysrepo::Datastore::Operational);
    DatastoreWatcher alarmInventoryChanges(client, "/ietf-alarms:alarms/alarm-inventory");
    RPCWatcher alarmRPC(client, "/sysrepo-ietf-alarms:create-or-update-alarm");

    REQUIRE_DATASTORE_CHANGE(alarmInventoryChanges,
                             (ValueChanges{
                                 {ALARM_INVENTORY_PATH("velia-alarms:systemd-unit-failure", "") "/alarm-type-id", "velia-alarms:systemd-unit-failure"},
                                 {ALARM_INVENTORY_PATH("velia-alarms:systemd-unit-failure", "") "/alarm-type-qualifier", ""},
                                 {ALARM_INVENTORY_PATH("velia-alarms:systemd-unit-failure", "") "/description", "The systemd service is considered in failed state."},
                                 {ALARM_INVENTORY_PATH("velia-alarms:systemd-unit-failure", "") "/severity-level[1]", "critical"},
                                 {ALARM_INVENTORY_PATH("velia-alarms:systemd-unit-failure", "") "/will-clear", "true"},
                             })).IN_SEQUENCE(seq1).ALARM_INVENTORY_INSERT(alarmInventory, "velia-alarms:systemd-unit-failure", "", std::set<std::string>{}, std::set<std::string>{"critical"});

    REQUIRE_ALARM_INVENTORY_UNIT("unit1.service");
    REQUIRE_ALARM_RPC("unit1.service", "cleared", "systemd unit state: (active, running)");
    server.createUnit(*serverConnection, "unit1.service", "/org/freedesktop/systemd1/unit/unit1", "active", "running");

    REQUIRE_ALARM_INVENTORY_UNIT("unit2.service");
    REQUIRE_ALARM_RPC("unit2.service", "critical", "systemd unit state: (activating, auto-restart)");
    server.createUnit(*serverConnection, "unit2.service", "/org/freedesktop/systemd1/unit/unit2", "activating", "auto-restart");

    REQUIRE_ALARM_INVENTORY_UNIT("unit3.service");
    REQUIRE_ALARM_RPC("unit3.service", "critical", "systemd unit state: (failed, failed)");
    server.createUnit(*serverConnection, "unit3.service", "/org/freedesktop/systemd1/unit/unit3", "failed", "failed");

    auto systemdAlarms = std::make_shared<velia::health::SystemdUnits>(srSess, *clientConnection, serverConnection->getUniqueName(), "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit");

    REQUIRE_ALARM_RPC("unit2.service", "cleared", "systemd unit state: (active, running)");
    REQUIRE_ALARM_RPC("unit3.service", "cleared", "systemd unit state: (active, running)");
    REQUIRE_ALARM_INVENTORY_UNIT("unit4.service");
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
