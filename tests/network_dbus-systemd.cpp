#include "trompeloeil_doctest.h"
#include "network/SystemdNetworkdDbusClient.h"
#include "tests/configure.cmake.h"
#include "tests/dbus-helpers/dbus_network1_server.h"
#include "tests/pretty_printers.h"
#include "tests/test_log_setup.h"

using namespace std::string_literals;

TEST_CASE("Reading data from systemd-networkd")
{
    TEST_SYSREPO_INIT_LOGS;

    auto dbusConnServer = sdbus::createSessionBusConnection();
    auto dbusConnClient = sdbus::createSessionBusConnection();

    dbusConnServer->enterEventLoopAsync();
    dbusConnClient->enterEventLoopAsync();

    std::vector<DbusNetwork1Server::LinkState> linkStates;
    std::vector<std::string> expected;

    SECTION("All possible states")
    {
        // systemd add1bc28d30bfb3ee2ccc804221a635cf188b733 networkd-link.c, link_state_table
        linkStates = {
            {"eth0", "pending"},
            {"eth1", "initialized"},
            {"eth2", "configuring"},
            {"eth3", "configured"},
            {"eth4", "unmanaged"},
            {"eth5", "failed"},
            {"eth6", "linger"},
        };
        expected = {"eth0", "eth1", "eth2", "eth3", "eth5", "eth6"};
    }

    SECTION("No links reported")
    {
        linkStates = {};
        expected = {};
    }

    SECTION("No links managed")
    {
        linkStates = {{"lo", "unmanaged"}, {"eth0", "unmanaged"}};
        expected = {};
    }

    DbusNetwork1Server dbusServer(*dbusConnServer, linkStates);

    velia::network::SystemdNetworkdDbusClient client(*dbusConnClient, dbusConnServer->getUniqueName(), "/org/freedesktop/network1");
    REQUIRE(client.getManagedLinks() == expected);
}
