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
#include "health/alarms/SystemdUnits.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "utils/log-init.h"
#include "utils/log.h"

using namespace std::chrono_literals;

// clang-format off
#define EXPECT_ALARM_RPC(RESOURCE, SEVERITY, TEXT) REQUIRE_CALL(fakeAlarmServer, rpcCalled(alarmRPC, std::map<std::string, std::string>{ \
			{"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", TEXT},                                                            \
			{"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:systemd-unit-failure"},                          \
			{"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},                                                    \
			{"/sysrepo-ietf-alarms:create-or-update-alarm/resource", RESOURCE},                                                          \
			{"/sysrepo-ietf-alarms:create-or-update-alarm/severity", SEVERITY}                                                           \
		})).IN_SEQUENCE(seq1);
// clang-format on

const auto alarmRPC = "/sysrepo-ietf-alarms:create-or-update-alarm";

class FakeAlarmServerSysrepo {
public:
    FakeAlarmServerSysrepo();
    MAKE_CONST_MOCK2(rpcCalled, void(std::string_view, const std::map<std::string, std::string>&));

private:
    sysrepo::Session m_srSess;
    std::optional<sysrepo::Subscription> m_srSub;
};


FakeAlarmServerSysrepo::FakeAlarmServerSysrepo()
    : m_srSess(sysrepo::Connection{}.sessionStart())
{
    m_srSub = m_srSess.onRPCAction(alarmRPC, [&](auto, auto, std::string_view path, const libyang::DataNode input, auto, auto, auto) {
        std::map<std::string, std::string> in;

        for (auto n : input.childrenDfs()) {
            if (n.isTerm()) {
                in[n.path()] = n.asTerm().valueStr();
            }
        }

        rpcCalled(path, in);
        return sysrepo::ErrorCode::Ok;
    });
}

TEST_CASE("systemd unit state monitoring (alarms)")
{
    TEST_INIT_LOGS;
    TEST_SYSREPO_INIT;
    trompeloeil::sequence seq1;

    // Create and setup separate connections for both client and server. Could be done using a single connection but this way it is more generic
    auto clientConnection = sdbus::createSessionBusConnection();
    auto serverConnection = sdbus::createSessionBusConnection();
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    auto server = DbusSystemdServer(*serverConnection);
    FakeAlarmServerSysrepo fakeAlarmServer;

    EXPECT_ALARM_RPC("unit1.service", "cleared", "systemd unit state: (active, running)");
    server.createUnit(*serverConnection, "unit1.service", "/org/freedesktop/systemd1/unit/unit1", "active", "running");
    EXPECT_ALARM_RPC("unit2.service", "critical", "systemd unit state: (activating, auto-restart)");
    server.createUnit(*serverConnection, "unit2.service", "/org/freedesktop/systemd1/unit/unit2", "activating", "auto-restart");
    EXPECT_ALARM_RPC("unit3.service", "critical", "systemd unit state: (failed, failed)");
    server.createUnit(*serverConnection, "unit3.service", "/org/freedesktop/systemd1/unit/unit3", "failed", "failed");

    auto systemdAlarms = std::make_shared<velia::health::SystemdUnits>(srSess, *clientConnection, serverConnection->getUniqueName(), "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "org.freedesktop.systemd1.Unit");

    waitForCompletionAndBitMore(seq1);
    // clang-format off
    REQUIRE(dataFromSysrepo(srSess, "/ietf-alarms:alarms/alarm-inventory", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
    		{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']", ""},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/alarm-type-id", "velia-alarms:systemd-unit-failure"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[1]", "unit1.service"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[2]", "unit2.service"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[3]", "unit3.service"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/will-clear", "true"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/severity-level[1]", "critical"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/description", "Alarm triggers when corresponding systemd unit fails."}
			});
    // clang-format on

    EXPECT_ALARM_RPC("unit2.service", "cleared", "systemd unit state: (active, running)");
    EXPECT_ALARM_RPC("unit3.service", "cleared", "systemd unit state: (active, running)");
    EXPECT_ALARM_RPC("unit4.service", "critical", "systemd unit state: (failed, failed)");
    EXPECT_ALARM_RPC("unit3.service", "critical", "systemd unit state: (activating, auto-restart)");
    EXPECT_ALARM_RPC("unit3.service", "cleared", "systemd unit state: (active, running)");
    EXPECT_ALARM_RPC("unit3.service", "critical", "systemd unit state: (failed, failed)");
    EXPECT_ALARM_RPC("unit3.service", "critical", "systemd unit state: (activating, auto-restart)");
    EXPECT_ALARM_RPC("unit3.service", "cleared", "systemd unit state: (active, running)");
    EXPECT_ALARM_RPC("unit4.service", "cleared", "systemd unit state: (active, running)");

    std::thread eventThr([&] {
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit2", "active", "running");
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");
        std::this_thread::sleep_for(25ms); // silence for a while

        // In case we obtain a notifications that unit changed state from (X,Y) to (X,Y), do not trigger any events.
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");
        std::this_thread::sleep_for(25ms);

        // add new unit with failed/failed, DbusSystemdInput should receive UnitNew signal and monitor this unit too
        server.createUnit(*serverConnection, "unit4.service", "/org/freedesktop/systemd1/unit/unit4", "failed", "failed");
        std::this_thread::sleep_for(25ms);

        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "activating", "auto-restart");
        std::this_thread::sleep_for(10ms);
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");
        std::this_thread::sleep_for(5ms);
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "failed", "failed");
        std::this_thread::sleep_for(5ms);
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "activating", "auto-restart");
        std::this_thread::sleep_for(10ms);
        server.changeUnitState("/org/freedesktop/systemd1/unit/unit3", "active", "running");

        server.changeUnitState("/org/freedesktop/systemd1/unit/unit4", "active", "running");
    });

    eventThr.join();
    waitForCompletionAndBitMore(seq1);

    // clang-format off
    REQUIRE(dataFromSysrepo(srSess, "/ietf-alarms:alarms/alarm-inventory", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
    		{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']", ""},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/alarm-type-id", "velia-alarms:systemd-unit-failure"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[1]", "unit1.service"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[2]", "unit2.service"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[3]", "unit3.service"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/resource[4]", "unit4.service"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/will-clear", "true"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/severity-level[1]", "critical"},
			{"/alarm-type[alarm-type-id='velia-alarms:systemd-unit-failure'][alarm-type-qualifier='']/description", "Alarm triggers when corresponding systemd unit fails."}
			});
    // clang-format on
}
