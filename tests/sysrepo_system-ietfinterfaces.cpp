/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "system/IETFInterfacesConfig.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "tests/mock/system.h"
#include "utils/io.h"

using namespace std::string_literals;

TEST_CASE("ietf-interfaces localhost")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess);

    // We didn't came up with some way of mocking netlink. At least check that there is the loopback
    // interface with expected values. It is *probably* safe to assume that there is at least the lo device.
    auto lo = dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='lo']", SR_DS_OPERATIONAL);

    // ensure statistics keys exist and remove them ; we can't really predict the content
    for (const auto& key : {"/statistics", "/statistics/in-discards", "/statistics/in-errors", "/statistics/in-octets", "/statistics/out-discards", "/statistics/out-errors", "/statistics/out-octets"}) {
        auto it = lo.find(key);
        REQUIRE(it != lo.end());
        lo.erase(it);
    }

    REQUIRE(lo == std::map<std::string, std::string> {
                {"/name", "lo"},
                {"/type", "iana-if-type:softwareLoopback"},
                {"/phys-address", "00:00:00:00:00:00"},
                {"/oper-status", "unknown"},
                {"/ietf-ip:ipv4", ""},
                {"/ietf-ip:ipv4/address[ip='127.0.0.1']", ""},
                {"/ietf-ip:ipv4/address[ip='127.0.0.1']/ip", "127.0.0.1"},
                {"/ietf-ip:ipv4/address[ip='127.0.0.1']/prefix-length", "8"},
                {"/ietf-ip:ipv6", ""},
                {"/ietf-ip:ipv6/address[ip='::1']", ""},
                {"/ietf-ip:ipv6/address[ip='::1']/ip", "::1"},
                {"/ietf-ip:ipv6/address[ip='::1']/prefix-length", "128"},
            });
    // NOTE: There are no neighbours on loopback

    SECTION("Link changes disabled")
    {
        client->session_switch_ds(SR_DS_STARTUP);

        SECTION("Only specified link names can appear in configurable datastore")
        {
            for (const auto& [name, type] : {std::pair<const char*, const char*>{"eth0", "iana-if-type:ethernetCsmacd"},
                                             {"eth1", "iana-if-type:ethernetCsmacd"},
                                             {"br0", "iana-if-type:bridge"},
                                             {"osc", "iana-if-type:ethernetCsmacd"},
                                             {"oscW", "iana-if-type:ethernetCsmacd"},
                                             {"oscE", "iana-if-type:ethernetCsmacd"}}) {
                client->set_item_str(("/ietf-interfaces:interfaces/interface[name='"s + name + "']/type").c_str(), type);
                client->set_item_str(("/ietf-interfaces:interfaces/interface[name='"s + name + "']/enabled").c_str(), "false");
            }
            client->apply_changes();
        }

        SECTION("Invalid type for a valid link")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:softwareLoopback");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }

        SECTION("Invalid name")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='blah0']/type", "iana-if-type:ethernetCsmacd");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }
    }

    SECTION("There must always be enabled protocol or the interface must be explicitely disabled")
    {
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");

        SECTION("Disabled protocols; enabled link")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }

        SECTION("Active protocols; disabled link")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client->apply_changes();
        }

        SECTION("IPv4 only; enabled link")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            client->apply_changes();
        }
    }
}

struct FakeNetworkReload {
public:
    MAKE_CONST_MOCK1(cb, void(const std::vector<std::string>&));
};

TEST_CASE("Config data in ietf-interfaces")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    trompeloeil::sequence seq1;

    auto fake = FakeNetworkReload();

    const auto fakeConfigDir = std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "tests/network/"s;
    std::filesystem::remove_all(fakeConfigDir);
    std::filesystem::create_directories(fakeConfigDir);

    auto expectedFilePath = fakeConfigDir / "eth0.network";

    auto network = std::make_shared<velia::system::IETFInterfacesConfig>(srSess, fakeConfigDir, std::vector<std::string>{"lo", "eth0"}, [&fake](const std::vector<std::string>& updatedInterfaces) { fake.cb(updatedInterfaces); });

    std::string expectedContents;
    SECTION("With description")
    {
        expectedContents = R"([Match]
Name=eth0

[Network]
Description=Hello world
Bridge=br0
LLDP=true
EmitLLDP=nearest-bridge
)";

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/description", "Hello world");
    }

    SECTION("No description")
    {
        expectedContents = R"([Match]
Name=eth0

[Network]
Bridge=br0
LLDP=true
EmitLLDP=nearest-bridge
)";
    }

    client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
    client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");

    REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
    client->apply_changes();
    REQUIRE(std::filesystem::exists(expectedFilePath));
    REQUIRE(velia::utils::readFileToString(expectedFilePath) == expectedContents);

    // reset the contents
    client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']");
    REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
    client->apply_changes();
    REQUIRE(!std::filesystem::exists(expectedFilePath));
}
