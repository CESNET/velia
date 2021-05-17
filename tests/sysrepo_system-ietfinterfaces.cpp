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
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "tests/mock/system.h"
#include "utils/io.h"

using namespace std::string_literals;

std::map<std::string, std::string> CONFIG = {
    {"Address=127.0.0.1/8", "[Match]\n"
                            "Name=lo\n"
                            "\n"
                            "[Network]\n"
                            "LLDP=true\n"
                            "EmitLLDP=nearest-bridge\n"
                            "DHCP=no\n"
                            "Address=127.0.0.1/8\n"},
    {"Address=127.0.0.{1,2}/8", "[Match]\n"
                                "Name=lo\n"
                                "\n"
                                "[Network]\n"
                                "LLDP=true\n"
                                "EmitLLDP=nearest-bridge\n"
                                "DHCP=no\n"
                                "Address=127.0.0.1/8\n"
                                "Address=127.0.0.2/8\n"},
    {"Address=127.0.0.1/8,Address=::1/128", "[Match]\n"
                                            "Name=lo\n"
                                            "\n"
                                            "[Network]\n"
                                            "LLDP=true\n"
                                            "EmitLLDP=nearest-bridge\n"
                                            "DHCP=no\n"
                                            "Address=127.0.0.1/8\n"
                                            "Address=::1/128\n"},

};

TEST_CASE("ietf-interfaces localhost")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto fakeConfigDir = std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "tests/network/"s;
    auto expectedFilePath = fakeConfigDir / "lo.network"s;

    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess, fakeConfigDir);

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

    SECTION("Setting IPs to interfaces")
    {
        client->session_switch_ds(SR_DS_STARTUP);

        std::string expectedContents;

        // reset the contents
        client->delete_item("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv4");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv6");
        client->apply_changes();

        SECTION("Single IPv4 address")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv4/ietf-ip:address[ip='127.0.0.1']/ietf-ip:prefix-length", "8");
            expectedContents = CONFIG["Address=127.0.0.1/8"];

            SECTION("Add another")
            {
                client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv4/ietf-ip:address[ip='127.0.0.2']/ietf-ip:prefix-length", "8");
                expectedContents = CONFIG["Address=127.0.0.{1,2}/8"];
            }
        }

        SECTION("Two IPv4 addresses")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv4/ietf-ip:address[ip='127.0.0.1']/ietf-ip:prefix-length", "8");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv4/ietf-ip:address[ip='127.0.0.2']/ietf-ip:prefix-length", "8");
            expectedContents = CONFIG["Address=127.0.0.{1,2}/8"];
        }

        SECTION("IPv4 and IPv6 addresses")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv4/ietf-ip:address[ip='127.0.0.1']/ietf-ip:prefix-length", "8");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv6/ietf-ip:address[ip='::1']/ietf-ip:prefix-length", "128");
            expectedContents = CONFIG["Address=127.0.0.1/8,Address=::1/128"];
        }

        client->apply_changes();
        REQUIRE(std::filesystem::exists(expectedFilePath));
        REQUIRE(velia::utils::readFileToString(expectedFilePath) == expectedContents);
    }
}
