/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/mock/system.h"

using namespace std::string_literals;

TEST_CASE("ietf-interfaces localhost")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;

    TEST_SYSREPO_INIT_CLIENT;

    std::map<std::string, std::string> systemLinks{{"eth0", "iana-if-type:ethernetCsmacd"},
            {"eth1", "iana-if-type:ethernetCsmacd"},
            {"br0", "iana-if-type:bridge"},
            {"lo", "iana-if-type:softwareLoopback"},
            {"osc", "iana-if-type:ethernetCsmacd"},
            {"sit0", "iana-if-type:sixToFour"}};
    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess, systemLinks);

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
                {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements", ""},
                {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements/prefix-list", ""},
            });
    // NOTE: There are no neighbours on loopback

    SECTION("Link changes disabled")
    {
        client->session_switch_ds(SR_DS_STARTUP);

        SECTION("Valid link and type")
        {
            for (const auto& [name, type] : systemLinks) {
                client->set_item_str(("/ietf-interfaces:interfaces/interface[name='"s + name + "']/type").c_str(), type.c_str());
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
}
