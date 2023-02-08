/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "trompeloeil_doctest.h"
#include <boost/process.hpp>
#include <filesystem>
#include <sysrepo-cpp/utils/exception.hpp>
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "system/IETFInterfacesConfig.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_vars.h"
#include "tests/configure.cmake.h"
#include "tests/mock/system.h"
#include "utils/io.h"

using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

const auto IFACE = "czechlight0"s;
const auto LINK_MAC = "02:02:02:02:02:02"s;
const auto WAIT = 500ms;
const auto WAIT_BRIDGE = 2500ms;

template <class... Args>
void iproute2_run(const Args... args_)
{
    namespace bp = boost::process;
    auto logger = spdlog::get("main");

    bp::ipstream stdoutStream;
    bp::ipstream stderrStream;

    std::vector<std::string> args = {IPROUTE2_EXECUTABLE, args_...};

    logger->trace("exec: {}", boost::algorithm::join(args, " "));
    bp::child c(boost::process::args = std::move(args), bp::std_out > stdoutStream, bp::std_err > stderrStream);
    c.wait();
    logger->trace("{} exited", IPROUTE2_EXECUTABLE);

    if (c.exit_code() != 0) {
        std::istreambuf_iterator<char> begin(stderrStream), end;
        std::string stderrOutput(begin, end);
        logger->critical("{} ended with a non-zero exit code. stderr: {}", IPROUTE2_EXECUTABLE, stderrOutput);

        throw std::runtime_error(IPROUTE2_EXECUTABLE + " returned non-zero exit code "s + std::to_string(c.exit_code()));
    }
}

template <class... Args>
void iproute2_exec_and_wait(const auto& wait, const Args... args_)
{
    iproute2_run(args_...);
    std::this_thread::sleep_for(wait); // wait for velia to process and publish the change
}

auto dataFromSysrepoNoStatistics(sysrepo::Session session, const std::string& xpath, sysrepo::Datastore datastore)
{
    auto res = dataFromSysrepo(session, xpath, datastore);
    REQUIRE(res.erase("/statistics/in-octets") == 1);
    REQUIRE(res.erase("/statistics/in-errors") == 1);
    REQUIRE(res.erase("/statistics/in-discards") == 1);
    REQUIRE(res.erase("/statistics/out-octets") == 1);
    REQUIRE(res.erase("/statistics/out-errors") == 1);
    REQUIRE(res.erase("/statistics/out-discards") == 1);
    return res;
}
}

TEST_CASE("ietf-interfaces localhost")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess);

    // We didn't came up with some way of mocking netlink. At least check that there is the loopback
    // interface with expected values. It is *probably* safe to assume that there is at least the lo device.
    auto lo = dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='lo']", sysrepo::Datastore::Operational);

    // ensure statistics keys exist and remove them ; we can't really predict the content
    for (const auto& key : {"/statistics/in-discards", "/statistics/in-errors", "/statistics/in-octets", "/statistics/out-discards", "/statistics/out-errors", "/statistics/out-octets"}) {
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
                {"/statistics", ""},
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

    sysrepo::Connection{}.sessionStart(sysrepo::Datastore::Running).copyConfig(sysrepo::Datastore::Startup, "ietf-interfaces");

    auto fake = FakeNetworkReload();

    const auto fakeConfigDir = std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "tests/network/"s;
    std::filesystem::remove_all(fakeConfigDir);
    std::filesystem::create_directories(fakeConfigDir);

    REQUIRE_CALL(fake, cb(std::vector<std::string>{})).IN_SEQUENCE(seq1);
    auto network = std::make_shared<velia::system::IETFInterfacesConfig>(srSess, fakeConfigDir, std::vector<std::string>{"br0", "eth0", "eth1"}, [&fake](const std::vector<std::string>& updatedInterfaces) { fake.cb(updatedInterfaces); });

    SECTION("Link changes disabled")
    {
        SECTION("Only specified link names can appear in configurable datastore")
        {
            for (const auto& [name, type] : {std::pair<const char*, const char*>{"eth0", "iana-if-type:ethernetCsmacd"},
                                             {"eth1", "iana-if-type:ethernetCsmacd"},
                                             {"br0", "iana-if-type:bridge"},
                                             {"osc", "iana-if-type:ethernetCsmacd"},
                                             {"oscW", "iana-if-type:ethernetCsmacd"},
                                             {"oscE", "iana-if-type:ethernetCsmacd"}}) {
                client.setItem("/ietf-interfaces:interfaces/interface[name='"s + name + "']/type", type);
                client.setItem("/ietf-interfaces:interfaces/interface[name='"s + name + "']/enabled", "false");
            }

            REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0", "eth1"})).IN_SEQUENCE(seq1); // only these are monitored by the test
            client.applyChanges();
        }

        SECTION("Invalid type for a valid link")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:softwareLoopback");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }

        SECTION("Invalid name")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='blah0']/type", "iana-if-type:ethernetCsmacd");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }
    }

    SECTION("There must always be enabled protocol or the interface must be explicitely disabled")
    {
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");

        SECTION("Disabled protocols; enabled link")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }

        SECTION("Active protocols; disabled link")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client.applyChanges();
        }

        SECTION("IPv4 only; enabled link")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client.applyChanges();
        }
    }

    SECTION("Every active protocol must have at least one IP address assigned")
    {
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "false");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1).TIMES(AT_MOST(1));
        client.applyChanges();

        SECTION("Enabled IPv4 protocol with some IPs assigned is valid")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.2']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client.applyChanges();
        }

        SECTION("Enabled IPv6 protocol with some IPs assigned is valid")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
            client.applyChanges();
        }

        SECTION("Enabled IPv4 protocol must have at least one IP or the autoconfiguration must be on")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "false");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }

        SECTION("Enabled IPv6 protocol must have at least one IP or the autoconfiguration must be on")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:enabled", "false");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:enabled", "true");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses", "false");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }
    }

    std::string expectedContents;

    SECTION("Setting IPs to eth0")
    {
        const auto expectedFilePath = fakeConfigDir / "eth0.network";

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

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(std::filesystem::exists(expectedFilePath));
        REQUIRE(velia::utils::readFileToString(expectedFilePath) == expectedContents);

        // reset the contents
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
        client.applyChanges();
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

        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/czechlight-network:dhcp-client", "false");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0", "eth1"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

        // reset the contents
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']");
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth1']");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0", "eth1"})).IN_SEQUENCE(seq1);
        client.applyChanges();
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

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0", "eth1"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

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

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

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

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

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

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth1"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(std::filesystem::exists(expectedFilePathEth1));
        REQUIRE(velia::utils::readFileToString(expectedFilePathBr0) == expectedContentsBr0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth0) == expectedContentsEth0);
        REQUIRE(velia::utils::readFileToString(expectedFilePathEth1) == expectedContentsEth1);

        // reset the contents
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='br0']");
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']");
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth1']");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0", "eth1"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(!std::filesystem::exists(expectedFilePathBr0));
        REQUIRE(!std::filesystem::exists(expectedFilePathEth0));
        REQUIRE(!std::filesystem::exists(expectedFilePathEth1));
    }

    SECTION("Slave interface and enabled/disabled IP protocols")
    {
        const auto expectedFilePathBr0 = fakeConfigDir / "br0.network";
        const auto expectedFilePathEth0 = fakeConfigDir / "eth0.network";

        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/type", "iana-if-type:bridge");
        client.setItem("/ietf-interfaces:interfaces/interface[name='br0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd");
        client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/czechlight-network:bridge", "br0");

        SECTION("Can't be a slave when IPv4 enabled")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }

        SECTION("Can't be a slave when IPv6 enabled")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }

        SECTION("Can't be a slave when both IPv4 and IPv6 enabled")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv6/ietf-ip:address[ip='2001:db8::1']/ietf-ip:prefix-length", "32");
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }

        SECTION("Can be a slave when addresses present but protocol is disabled")
        {
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/ietf-ip:address[ip='192.0.2.1']/ietf-ip:prefix-length", "24");
            client.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/enabled", "false");

            REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0"})).IN_SEQUENCE(seq1);
            client.applyChanges();

            // reset the contents
            client.deleteItem("/ietf-interfaces:interfaces/interface[name='br0']");
            client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']");
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"br0", "eth0"})).IN_SEQUENCE(seq1);
            client.applyChanges();
            REQUIRE(!std::filesystem::exists(expectedFilePathBr0));
            REQUIRE(!std::filesystem::exists(expectedFilePathEth0));
        }
    }

    SECTION("Network autoconfiguration")
    {
        const auto expectedFilePath = fakeConfigDir / "eth0.network";

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

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(std::filesystem::exists(expectedFilePath));
        REQUIRE(velia::utils::readFileToString(expectedFilePath) == expectedContents);

        // reset the contents
        client.deleteItem("/ietf-interfaces:interfaces/interface[name='eth0']");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth0"})).IN_SEQUENCE(seq1);
        client.applyChanges();
        REQUIRE(!std::filesystem::exists(expectedFilePath));
    }
}

TEST_CASE("ietf-interfaces and ietf-routing listen to changes")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess);

    iproute2_exec_and_wait(WAIT, "link", "add", IFACE, "address", LINK_MAC, "type", "dummy");

    iproute2_exec_and_wait(WAIT, "addr", "add", "192.0.2.1/24", "dev", IFACE); // from TEST-NET-1 (RFC 5737)
    iproute2_exec_and_wait(WAIT, "addr", "add", "::ffff:192.0.2.1", "dev", IFACE);

    std::map<std::string, std::string> initialExpected{
        {"/ietf-ip:ipv4", ""},
        {"/ietf-ip:ipv4/address[ip='192.0.2.1']", ""},
        {"/ietf-ip:ipv4/address[ip='192.0.2.1']/ip", "192.0.2.1"},
        {"/ietf-ip:ipv4/address[ip='192.0.2.1']/prefix-length", "24"},
        {"/ietf-ip:ipv6", ""},
        {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']", ""},
        {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip", "::ffff:192.0.2.1"},
        {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length", "128"},
        {"/ietf-ip:ipv6/autoconf", ""},
        {"/name", IFACE},
        {"/oper-status", "down"},
        {"/phys-address", LINK_MAC},
        {"/statistics", ""},
        {"/type", "iana-if-type:ethernetCsmacd"},
    };

    SECTION("Change physical address")
    {
        const auto LINK_MAC_CHANGED = "02:44:44:44:44:44"s;

        iproute2_exec_and_wait(WAIT, "link", "set", IFACE, "address", LINK_MAC_CHANGED);

        std::map<std::string, std::string> expected = initialExpected;
        expected["/phys-address"] = LINK_MAC_CHANGED;
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expected);
    }

    SECTION("Add and remove IP addresses")
    {
        iproute2_exec_and_wait(WAIT, "addr", "add", "192.0.2.6/24", "dev", IFACE);
        std::map<std::string, std::string> expected = initialExpected;
        expected["/ietf-ip:ipv4/address[ip='192.0.2.6']"] = "";
        expected["/ietf-ip:ipv4/address[ip='192.0.2.6']/ip"] = "192.0.2.6";
        expected["/ietf-ip:ipv4/address[ip='192.0.2.6']/prefix-length"] = "24";
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expected);

        iproute2_exec_and_wait(WAIT, "addr", "del", "192.0.2.6/24", "dev", IFACE);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == initialExpected);
    }

    SECTION("IPv6 LL gained when device up")
    {
        iproute2_exec_and_wait(WAIT, "link", "set", "dev", IFACE, "up");

        {
            std::map<std::string, std::string> expected = initialExpected;
            expected["/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']"] = "";
            expected["/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']/ip"] = "fe80::2:2ff:fe02:202";
            expected["/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']/prefix-length"] = "64";
            expected["/oper-status"] = "unknown";
            REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expected);
        }

        iproute2_exec_and_wait(WAIT, "link", "set", "dev", IFACE, "down"); // this discards all addresses, i.e., the link-local address and the ::ffff:192.0.2.1 address
        {
            std::map<std::string, std::string> expected = initialExpected;
            expected.erase("/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']");
            expected.erase("/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip");
            expected.erase("/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length");
            expected["/oper-status"] = "down";
            REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expected);
        }
    }

    SECTION("Add a bridge")
    {
        const auto IFACE_BRIDGE = "czechlight_br0"s;
        const auto MAC_BRIDGE = "02:22:22:22:22:22";

        std::map<std::string, std::string> expectedIface = initialExpected;
        std::map<std::string, std::string> expectedBridge{
            {"/name", "czechlight_br0"},
            {"/oper-status", "down"},
            {"/phys-address", MAC_BRIDGE},
            {"/statistics", ""},
            {"/type", "iana-if-type:bridge"},
        };

        iproute2_exec_and_wait(WAIT, "link", "add", "name", IFACE_BRIDGE, "address", MAC_BRIDGE, "type", "bridge");
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expectedIface);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE_BRIDGE + "']", sysrepo::Datastore::Operational) == expectedBridge);

        iproute2_exec_and_wait(WAIT, "link", "set", "dev", IFACE, "master", IFACE_BRIDGE);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expectedIface);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE_BRIDGE + "']", sysrepo::Datastore::Operational) == expectedBridge);

        iproute2_exec_and_wait(WAIT, "link", "set", "dev", IFACE, "up");
        iproute2_exec_and_wait(WAIT, "addr", "flush", "dev", IFACE); // sometimes, addresses are preserved even when enslaved
        expectedIface["/oper-status"] = "unknown";
        expectedIface.erase("/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']");
        expectedIface.erase("/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip");
        expectedIface.erase("/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length");
        expectedIface.erase("/ietf-ip:ipv4/address[ip='192.0.2.1']");
        expectedIface.erase("/ietf-ip:ipv4/address[ip='192.0.2.1']/ip");
        expectedIface.erase("/ietf-ip:ipv4/address[ip='192.0.2.1']/prefix-length");
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expectedIface);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE_BRIDGE + "']", sysrepo::Datastore::Operational) == expectedBridge);

        iproute2_exec_and_wait(WAIT_BRIDGE, "link", "set", "dev", IFACE_BRIDGE, "up");
        expectedBridge["/ietf-ip:ipv6"] = "";
        expectedBridge["/ietf-ip:ipv6/autoconf"] = "";
        expectedBridge["/ietf-ip:ipv6/address[ip='fe80::22:22ff:fe22:2222']"] = "";
        expectedBridge["/ietf-ip:ipv6/address[ip='fe80::22:22ff:fe22:2222']/ip"] = "fe80::22:22ff:fe22:2222";
        expectedBridge["/ietf-ip:ipv6/address[ip='fe80::22:22ff:fe22:2222']/prefix-length"] = "64";
        expectedBridge["/oper-status"] = "up";
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expectedIface);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE_BRIDGE + "']", sysrepo::Datastore::Operational) == expectedBridge);

        iproute2_exec_and_wait(WAIT_BRIDGE, "link", "set", "dev", IFACE_BRIDGE, "down");
        expectedBridge.erase("/ietf-ip:ipv6/address[ip='fe80::22:22ff:fe22:2222']");
        expectedBridge.erase("/ietf-ip:ipv6/address[ip='fe80::22:22ff:fe22:2222']/ip");
        expectedBridge.erase("/ietf-ip:ipv6/address[ip='fe80::22:22ff:fe22:2222']/prefix-length");
        expectedBridge["/oper-status"] = "down";
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expectedIface);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE_BRIDGE + "']", sysrepo::Datastore::Operational) == expectedBridge);

        iproute2_exec_and_wait(WAIT, "link", "set", "dev", IFACE, "down");
        expectedIface["/oper-status"] = "down";
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expectedIface);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE_BRIDGE + "']", sysrepo::Datastore::Operational) == expectedBridge);
        iproute2_exec_and_wait(WAIT, "link", "set", "dev", IFACE, "nomaster");
        expectedIface.erase("/ietf-ip:ipv4");
        expectedIface.erase("/ietf-ip:ipv6/autoconf");
        expectedIface.erase("/ietf-ip:ipv6");
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", sysrepo::Datastore::Operational) == expectedIface);
        REQUIRE(dataFromSysrepoNoStatistics(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE_BRIDGE + "']", sysrepo::Datastore::Operational) == expectedBridge);
    }

    SECTION("Add and remove routes")
    {
        iproute2_exec_and_wait(WAIT, "link", "set", "dev", IFACE, "up");
        iproute2_exec_and_wait(WAIT, "route", "add", "198.51.100.0/24", "dev", IFACE);
        std::this_thread::sleep_for(WAIT);

        auto data = dataFromSysrepo(client, "/ietf-routing:routing", sysrepo::Datastore::Operational);
        REQUIRE(data["/control-plane-protocols"] == "");
        REQUIRE(data["/interfaces"] == "");
        REQUIRE(data["/ribs"] == "");

        data = dataFromSysrepo(client, "/ietf-routing:routing/ribs/rib[name='ipv4-master']", sysrepo::Datastore::Operational);
        REQUIRE(data["/name"] == "ipv4-master");

        auto findRouteIndex = [&data](const std::string& prefix) {
            std::smatch match;
            std::regex regex(R"(route\[(\d+)\])");
            size_t length = 0;
            for (const auto& [key, value] : data) {
                if (std::regex_search(key, match, regex)) {
                    length = std::max(std::stoul(match[1]), length);
                }
            }

            for (size_t i = 1; i <= length; i++) {
                const auto keyPrefix = "/routes/route["s + std::to_string(i) + "]";
                if (data[keyPrefix + "/ietf-ipv4-unicast-routing:destination-prefix"] == prefix)
                    return i;
            }

            return size_t{0};
        };

        {
            auto routeIdx = findRouteIndex("198.51.100.0/24");
            REQUIRE(routeIdx > 0);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/next-hop/outgoing-interface"] == IFACE);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/source-protocol"] == "ietf-routing:static");
        }
        {
            auto routeIdx = findRouteIndex("192.0.2.0/24");
            REQUIRE(routeIdx > 0);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/next-hop/outgoing-interface"] == IFACE);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/source-protocol"] == "ietf-routing:direct");
        }

        data = dataFromSysrepo(client, "/ietf-routing:routing/ribs/rib[name='ipv6-master']", sysrepo::Datastore::Operational);
        REQUIRE(data["/name"] == "ipv6-master");

        iproute2_exec_and_wait(WAIT, "route", "del", "198.51.100.0/24");
        iproute2_exec_and_wait(WAIT, "link", "set", IFACE, "down");
    }

    iproute2_exec_and_wait(WAIT, "link", "del", IFACE, "type", "dummy"); // Executed later again by ctest fixture cleanup just for sure. It remains here because of doctest sections: The interface needs to be setup again.
}
