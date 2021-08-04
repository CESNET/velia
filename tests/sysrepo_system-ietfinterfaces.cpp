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

    REQUIRE(lo == std::map<std::string, std::string>{
                {"/name", "lo"},
                {"/type", "iana-if-type:softwareLoopback"},
                {"/phys-address", "00:00:00:00:00:00"},
                {"/oper-status", "unknown"},
                {"/ietf-ip:ipv4", ""},
                {"/ietf-ip:ipv4/address[ip='127.0.0.1']", ""},
                {"/ietf-ip:ipv4/address[ip='127.0.0.1']/ip", "127.0.0.1"},
                {"/ietf-ip:ipv4/address[ip='127.0.0.1']/prefix-length", "8"},
                {"/ietf-ip:ipv6", ""},
                {"/ietf-ip:ipv6/autoconf", ""},
                {"/ietf-ip:ipv6/address[ip='::1']", ""},
                {"/ietf-ip:ipv6/address[ip='::1']/ip", "::1"},
                {"/ietf-ip:ipv6/address[ip='::1']/prefix-length", "128"},
            });
    // NOTE: There are no neighbours on loopback
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

    auto network = std::make_shared<velia::system::IETFInterfacesConfig>(srSess, fakeConfigDir, std::vector<std::string>{"br0", "eth0", "eth1"}, [&fake](const std::vector<std::string>& updatedInterfaces) { fake.cb(updatedInterfaces); });

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
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client->apply_changes();
        }

        SECTION("IPv4 only; enabled link")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client->apply_changes();
        }
    }

    SECTION("Every active protocol must have at least one IP address assigned")
    {
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "false");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1).TIMES(AT_MOST(1));
        client->apply_changes();

        SECTION("Enabled IPv4 protocol with some IPs assigned is valid")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.2']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client->apply_changes();
        }

        SECTION("Enabled IPv6 protocol with some IPs assigned is valid")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client->apply_changes();
        }

        SECTION("Enabled IPv4 protocol must have at least one IP or the autoconfiguration must be on")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }

        SECTION("Enabled IPv6 protocol must have at least one IP or the aucoconfiguration must be on")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "false");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }
    }

    std::string expectedContents;

    SECTION("Setting IPs to eth0")
    {
        const auto expectedFilePath = fakeConfigDir / "eth0.network";

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");

        SECTION("Single IPv4 address")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/description", "Hello world");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            expectedContents = R"([Match]
Name=eth0

[Network]
Description=Hello world
Address=192.0.2.1/24
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("Two IPv4 addresses")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.2']/ietf-ip:prefix-length", "24");
            client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6");
            expectedContents = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
Address=192.0.2.2/24
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("IPv4 and IPv6 addresses")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            expectedContents = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
Address=2001:db8::1/32
IPv6AcceptRA=true
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("IPv4 and IPv6 addresses but IPv6 disabled")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/enabled", "false");
            expectedContents = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

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

    SECTION("Two links")
    {
        const auto expectedFilePathEth0 = fakeConfigDir / "eth0.network";
        const auto expectedFilePathEth1 = fakeConfigDir / "eth1.network";

        std::string expectedContentsEth0 = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        std::string expectedContentsEth1 = R"([Match]
Name=eth1

[Network]
Address=2001:db8::1/32
IPv6AcceptRA=true
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth1']/type", "iana-if-type:ethernetCsmacd");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0", "eth1"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

        // reset the contents
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth1']");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0", "eth1"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(!std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(!std::filesystem::exists(expectedFilePathEth1));
    }

    SECTION("Setup a bridge br0 over eth0 and eth1")
    {
        const auto expectedFilePathBr0 = fakeConfigDir / "br0.network";
        const auto expectedFilePathEth0 = fakeConfigDir / "eth0.network";
        const auto expectedFilePathEth1 = fakeConfigDir / "eth1.network";

        std::string expectedContentsBr0 = R"([Match]
Name=br0

[Network]
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        std::string expectedContentsEth0 = R"([Match]
Name=eth0

[Network]
Bridge=br0
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        std::string expectedContentsEth1 = R"([Match]
Name=eth1

[Network]
Bridge=br0
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        // create br0 bridge over eth0 and eth1 with no IP
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='br0']/enabled", "false");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='br0']/type", "iana-if-type:bridge");

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/czechlight-network:bridge", "br0");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4");

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth1']/type", "iana-if-type:ethernetCsmacd");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth1']/czechlight-network:bridge", "br0");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv4/ietf-ip:enabled", "false");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6");

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0", "eth1"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

        // assign an IPv4 address to br0
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='br0']/enabled", "true");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='br0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
        expectedContentsBr0 = R"([Match]
Name=br0

[Network]
Address=192.0.2.1/24
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

        // assign also an IPv6 address to br0
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='br0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
        expectedContentsBr0 = R"([Match]
Name=br0

[Network]
Address=192.0.2.1/24
Address=2001:db8::1/32
IPv6AcceptRA=true
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

        // remove eth1 from bridge
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth1']/czechlight-network:bridge");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::2']/ietf-ip:prefix-length", "32");

        expectedContentsEth1 = R"([Match]
Name=eth1

[Network]
Address=2001:db8::2/32
IPv6AcceptRA=true
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth1"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

        // reset the contents
        client->delete_item("/ietf-interfaces:interfaces/interface[name='br0']");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']");
        client->delete_item("/ietf-interfaces:interfaces/interface[name='eth1']");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0", "eth1"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(!std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(!std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(!std::filesystem::exists(expectedFilePathEth1));
    }

    SECTION("Slave interface and enabled/disabled IP protocols")
    {
        const auto expectedFilePathBr0 = fakeConfigDir / "br0.network";
        const auto expectedFilePathEth0 = fakeConfigDir / "eth0.network";

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='br0']/type", "iana-if-type:bridge");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='br0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/czechlight-network:bridge", "br0");

        SECTION("Can't be a slave when IPv4 enabled")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }

        SECTION("Can't be a slave when IPv6 enabled")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }

        SECTION("Can't be a slave when both IPv4 and IPv6 enabled")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_THROWS_AS(client->apply_changes(), sysrepo::sysrepo_exception);
        }

        SECTION("Can be a slave when addresses present but protocol is disabled")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/enabled", "false");

            REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0"})).IN_SEQUENCE(seq1);
            client->apply_changes();

            // reset the contents
            client->delete_item("/ietf-interfaces:interfaces/interface[name='br0']");
            client->delete_item("/ietf-interfaces:interfaces/interface[name='eth0']");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0"})).IN_SEQUENCE(seq1);
            client->apply_changes();
            REQUIRE(!std::filesystem::exists(expectedFilePathBr0));
            REQUIRE(!std::filesystem::exists(expectedFilePathEth0));
        }
    }

    SECTION("Network autoconfiguration")
    {
        const auto expectedFilePath = fakeConfigDir / "eth0.network";

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");

        SECTION("IPv4 on with address, IPv6 disabled, DHCPv4 off, RA off")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24"); // in case DHCP is disabled an IP must be present

            expectedContents = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("IPv4 on with address, IPv6 disabled, DHCPv4 on, RA on")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24"); // in case DHCP is disabled an IP must be present
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

            expectedContents = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
LinkLocalAddressing=no
IPv6AcceptRA=false
DHCP=ipv4
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("IPv4 disabled, IPv6 enabled, DHCPv4 on, RA on")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

            expectedContents = R"([Match]
Name=eth0

[Network]
IPv6AcceptRA=true
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("IPv4 enabled, IPv6 enabled, DHCPv4 on, RA on")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

            expectedContents = R"([Match]
Name=eth0

[Network]
IPv6AcceptRA=true
DHCP=ipv4
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("IPv4 enabled, IPv6 enabled, DHCPv4 off, RA on")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

            expectedContents = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
IPv6AcceptRA=true
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

        SECTION("IPv4 disabled, IPv6 disabled, DHCPv4 off, RA off")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/prefix-length", "24");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/prefix-length", "32");
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "false");

            expectedContents = R"([Match]
Name=eth0

[Network]
Address=192.0.2.1/24
Address=2001:db8::1/32
IPv6AcceptRA=false
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";
        }

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
}
