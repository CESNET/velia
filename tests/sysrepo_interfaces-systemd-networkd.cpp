/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include <sysrepo-cpp/utils/exception.hpp>
#include "pretty_printers.h"
#include "network/IETFInterfacesConfig.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/mock/system.h"
#include "tests/sysrepo-helpers/common.h"
#include "utils/io.h"

using namespace std::string_literals;
using ChangedUnits = velia::network::IETFInterfacesConfig::ChangedUnits;

const auto fakeConfigDir = std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "tests/network/"s;
#define NETWORK_FILE(LINK_NAME) fakeConfigDir / "10-" LINK_NAME ".network"
#define REQUIRE_NETWORK_CONFIGURATION(LINK_NAME, CONTENTS) \
    REQUIRE(std::filesystem::exists(NETWORK_FILE(LINK_NAME))); \
    REQUIRE(velia::utils::readFileToString(NETWORK_FILE(LINK_NAME)) == CONTENTS);
#define REQUIRE_NETWORK_EMPTY_CONFIGURATION(LINK_NAME) \
    REQUIRE(std::filesystem::exists(NETWORK_FILE(LINK_NAME))); \
    REQUIRE(velia::utils::readFileToString(NETWORK_FILE(LINK_NAME)) == R"([Match]
Name=)" LINK_NAME R"(
[Network]
DHCP=no
LinkLocalAddressing=no
IPv6AcceptRA=no
)");

struct FakeNetworkReload {
public:
    MAKE_CONST_MOCK1(cb, void(const ChangedUnits&));
};

TEST_CASE("Config data in ietf-interfaces")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    trompeloeil::sequence seq1;

    srSess.sendRPC(srSess.getContext().newPath("/ietf-factory-default:factory-reset"));

    sysrepo::Connection{}.sessionStart(sysrepo::Datastore::Running).copyConfig(sysrepo::Datastore::Startup, "ietf-interfaces");

    auto fake = FakeNetworkReload();

    std::filesystem::remove_all(fakeConfigDir);
    std::filesystem::create_directories(fakeConfigDir);

    REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {"br0", "eth0", "eth1"}, .changedOrNew = {}})).IN_SEQUENCE(seq1);
    auto network = std::make_shared<velia::network::IETFInterfacesConfig>(srSess, fakeConfigDir, std::vector<std::string>{"br0", "eth0", "eth1"}, [&fake](const ChangedUnits& update) { fake.cb(update); });

    std::string expectedContents;

    SECTION("Setting IPs to eth0")
    {
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");

        SECTION("Single IPv4 address")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/description", "Hello world");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.2']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6");
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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/enabled", "false");
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

        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {}, .changedOrNew = {"eth0"}})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE_NETWORK_CONFIGURATION("eth0", expectedContents);
    }

    SECTION("Two links")
    {
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

        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");

        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {}, .changedOrNew = {"eth0", "eth1"}})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE_NETWORK_CONFIGURATION("eth0", expectedContentsEth0);
        REQUIRE_NETWORK_CONFIGURATION("eth1", expectedContentsEth1);

        // Test removing link configuration
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']");
        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {"eth0"}, .changedOrNew = {}})).IN_SEQUENCE(seq1);
        client.applyChanges();

        REQUIRE_NETWORK_EMPTY_CONFIGURATION("eth0");
        REQUIRE_NETWORK_CONFIGURATION("eth1", expectedContentsEth1);
    }

    SECTION("Setup a bridge br0 over eth0 and eth1")
    {
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
        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/enabled", "false");
        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/type", "iana-if-type:bridge");

        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/czechlight-network:bridge", "br0");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4");

        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/czechlight-network:bridge", "br0");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv4/ietf-ip:enabled", "false");
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6");

        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {}, .changedOrNew = {"eth0", "eth1"}})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE_NETWORK_EMPTY_CONFIGURATION("br0");
        REQUIRE_NETWORK_CONFIGURATION("eth0", expectedContentsEth0);
        REQUIRE_NETWORK_CONFIGURATION("eth1", expectedContentsEth1);

        // assign an IPv4 address to br0
        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/enabled", "true");
        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
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

        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {}, .changedOrNew = {"br0"}})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE_NETWORK_CONFIGURATION("br0", expectedContentsBr0);
        REQUIRE_NETWORK_CONFIGURATION("eth0", expectedContentsEth0);
        REQUIRE_NETWORK_CONFIGURATION("eth1", expectedContentsEth1);

        // assign also an IPv6 address to br0
        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
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

        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {}, .changedOrNew = {"br0"}})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE_NETWORK_CONFIGURATION("br0", expectedContentsBr0);
        REQUIRE_NETWORK_CONFIGURATION("eth0", expectedContentsEth0);
        REQUIRE_NETWORK_CONFIGURATION("eth1", expectedContentsEth1);

        // remove eth1 from bridge
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth1']/czechlight-network:bridge");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::2']/ietf-ip:prefix-length", "32");

        expectedContentsEth1 = R"([Match]
Name=eth1

[Network]
Address=2001:db8::2/32
IPv6AcceptRA=true
DHCP=no
LLDP=true
EmitLLDP=nearest-bridge
)";

        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {}, .changedOrNew = {"eth1"}})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE_NETWORK_CONFIGURATION("br0", expectedContentsBr0);
        REQUIRE_NETWORK_CONFIGURATION("eth0", expectedContentsEth0);
        REQUIRE_NETWORK_CONFIGURATION("eth1", expectedContentsEth1);
    }

    SECTION("Network autoconfiguration")
    {
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");

        SECTION("IPv4 on with address, IPv6 disabled, DHCPv4 off, RA off")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24"); // in case DHCP is disabled an IP must be present

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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24"); // in case DHCP is disabled an IP must be present
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "true");

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
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/prefix-length", "32");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "false");

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

        REQUIRE_CALL(fake, cb(ChangedUnits{.deleted = {}, .changedOrNew = {"eth0"}})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE_NETWORK_CONFIGURATION("eth0", expectedContents);
    }
}
