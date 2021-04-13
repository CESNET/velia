/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <boost/algorithm/string/join.hpp>
#include <boost/process.hpp>
#include <boost/process/extend.hpp>
#include <cstdlib>
#include <regex>
#include <sys/wait.h>
#include <thread>
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_vars.h"

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

const auto IFACE = "czechlight0"s;
const auto LINK_MAC = "02:02:02:02:02:02"s;
const auto WAIT = 25ms;

template <class... Args>
void iproute2_run(const Args... args_)
{
    namespace bp = boost::process;
    auto logger = spdlog::get("main");

    bp::ipstream stdoutStream;
    bp::ipstream stderrStream;

    std::vector<std::string> args = {IPROUTE2_EXECUTABLE, args_...};

    logger->trace("exec: {} {}", SUDO_EXECUTABLE, boost::algorithm::join(args, " "));
    bp::child c(SUDO_EXECUTABLE, boost::process::args = std::move(args), bp::std_out > stdoutStream, bp::std_err > stderrStream);
    c.wait();
    logger->trace("{} {} exited", SUDO_EXECUTABLE, IPROUTE2_EXECUTABLE);

    if (c.exit_code() != 0) {
        std::istreambuf_iterator<char> begin(stderrStream), end;
        std::string stderrOutput(begin, end);
        logger->critical("{} {} ended with a non-zero exit code. stderr: {}", SUDO_EXECUTABLE, IPROUTE2_EXECUTABLE, stderrOutput);

        throw std::runtime_error(SUDO_EXECUTABLE + " "s + IPROUTE2_EXECUTABLE + " returned non-zero exit code " + std::to_string(c.exit_code()));
    }
}

template <class... Args>
void iproute2_exec_and_wait(const Args... args_)
{
    iproute2_run(args_...);
    std::this_thread::sleep_for(WAIT); // wait for velia to process and publish the change
}

}

TEST_CASE("Test ietf-interfaces and ietf-routing")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess);

    iproute2_exec_and_wait("link", "add", IFACE, "address", LINK_MAC, "type", "dummy");
    iproute2_exec_and_wait("addr", "flush", "dev", IFACE); // remove all addrs from link (this is because there may be some addresses left from other docopt's sections)

    iproute2_exec_and_wait("addr", "add", "192.0.2.1/24", "dev", IFACE); // from TEST-NET-1 (RFC 5737)
    iproute2_exec_and_wait("addr", "add", "::ffff:192.0.2.1", "dev", IFACE);

    REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                {"/ietf-ip:ipv4", ""},
                {"/ietf-ip:ipv4/address[ip='192.0.2.1']", ""},
                {"/ietf-ip:ipv4/address[ip='192.0.2.1']/ip", "192.0.2.1"},
                {"/ietf-ip:ipv4/address[ip='192.0.2.1']/prefix-length", "24"},
                {"/ietf-ip:ipv6", ""},
                {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']", ""},
                {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip", "::ffff:192.0.2.1"},
                {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length", "128"},
                {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements", ""},
                {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements/prefix-list", ""},
                {"/name", IFACE},
                {"/oper-status", "down"},
                {"/phys-address", LINK_MAC},
                {"/statistics", ""},
                {"/statistics/in-discards", "0"},
                {"/statistics/in-errors", "0"},
                {"/statistics/in-octets", "0"},
                {"/statistics/out-discards", "0"},
                {"/statistics/out-errors", "0"},
                {"/statistics/out-octets", "0"},
                {"/type", "iana-if-type:ethernetCsmacd"},
            });

    SECTION("Change physical address")
    {
        const auto LINK_MAC_CHANGED = "02:44:44:44:44:44"s;

        iproute2_exec_and_wait("link", "set", IFACE, "address", LINK_MAC_CHANGED);

        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/ietf-ip:ipv4", ""},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']", ""},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']/ip", "192.0.2.1"},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']/prefix-length", "24"},
                    {"/ietf-ip:ipv6", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip", "::ffff:192.0.2.1"},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length", "128"},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements", ""},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements/prefix-list", ""},
                    {"/name", IFACE},
                    {"/oper-status", "down"},
                    {"/phys-address", LINK_MAC_CHANGED},
                    {"/statistics", ""},
                    {"/statistics/in-discards", "0"},
                    {"/statistics/in-errors", "0"},
                    {"/statistics/in-octets", "0"},
                    {"/statistics/out-discards", "0"},
                    {"/statistics/out-errors", "0"},
                    {"/statistics/out-octets", "0"},
                    {"/type", "iana-if-type:ethernetCsmacd"},
                });
    }

    SECTION("Add and remove IP addresses")
    {
        iproute2_exec_and_wait("addr", "add", "192.0.2.6/24", "dev", IFACE);
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/ietf-ip:ipv4", ""},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']", ""},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']/ip", "192.0.2.1"},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']/prefix-length", "24"},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.6']", ""},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.6']/ip", "192.0.2.6"},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.6']/prefix-length", "24"},
                    {"/ietf-ip:ipv6", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip", "::ffff:192.0.2.1"},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length", "128"},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements", ""},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements/prefix-list", ""},
                    {"/name", IFACE},
                    {"/oper-status", "down"},
                    {"/phys-address", LINK_MAC},
                    {"/statistics", ""},
                    {"/statistics/in-discards", "0"},
                    {"/statistics/in-errors", "0"},
                    {"/statistics/in-octets", "0"},
                    {"/statistics/out-discards", "0"},
                    {"/statistics/out-errors", "0"},
                    {"/statistics/out-octets", "0"},
                    {"/type", "iana-if-type:ethernetCsmacd"},
                });

        iproute2_exec_and_wait("addr", "del", "192.0.2.6/24", "dev", IFACE);
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/ietf-ip:ipv4", ""},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']", ""},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']/ip", "192.0.2.1"},
                    {"/ietf-ip:ipv4/address[ip='192.0.2.1']/prefix-length", "24"},
                    {"/ietf-ip:ipv6", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip", "::ffff:192.0.2.1"},
                    {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length", "128"},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements", ""},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements/prefix-list", ""},
                    {"/name", IFACE},
                    {"/oper-status", "down"},
                    {"/phys-address", LINK_MAC},
                    {"/statistics", ""},
                    {"/statistics/in-discards", "0"},
                    {"/statistics/in-errors", "0"},
                    {"/statistics/in-octets", "0"},
                    {"/statistics/out-discards", "0"},
                    {"/statistics/out-errors", "0"},
                    {"/statistics/out-octets", "0"},
                    {"/type", "iana-if-type:ethernetCsmacd"},
                });
    }

    SECTION("IPv6 LL gained when device up")
    {
        iproute2_exec_and_wait("link", "set", "dev", IFACE, "up");
        std::this_thread::sleep_for(WAIT); // FIXME: why this needs more wait time? Usually fails without more time.

        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
            {"/ietf-ip:ipv4", ""},
            {"/ietf-ip:ipv4/address[ip='192.0.2.1']", ""},
            {"/ietf-ip:ipv4/address[ip='192.0.2.1']/ip", "192.0.2.1"},
            {"/ietf-ip:ipv4/address[ip='192.0.2.1']/prefix-length", "24"},
            {"/ietf-ip:ipv6", ""},
            {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']", ""},
            {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/ip", "::ffff:192.0.2.1"},
            {"/ietf-ip:ipv6/address[ip='::ffff:192.0.2.1']/prefix-length", "128"},
            {"/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']", ""},
            {"/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']/ip", "fe80::2:2ff:fe02:202"},
            {"/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']/prefix-length", "64"},
            {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements", ""},
            {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements/prefix-list", ""},
            {"/name", IFACE},
            {"/oper-status", "unknown"},
            {"/phys-address", LINK_MAC},
            {"/statistics", ""},
            {"/statistics/in-discards", "0"},
            {"/statistics/in-errors", "0"},
            {"/statistics/in-octets", "0"},
            {"/statistics/out-discards", "0"},
            {"/statistics/out-errors", "0"},
            {"/statistics/out-octets", "70"},
            {"/type", "iana-if-type:ethernetCsmacd"},
        });

        iproute2_exec_and_wait("link", "set", "dev", IFACE, "down");
    }

    SECTION("Add and remove routes")
    {
        iproute2_exec_and_wait("link", "set", "dev", IFACE, "up");
        iproute2_exec_and_wait("route", "add", "198.51.100.0/24", "dev", IFACE);
        std::this_thread::sleep_for(WAIT);

        auto data = dataFromSysrepo(client, "/ietf-routing:routing", SR_DS_OPERATIONAL);
        REQUIRE(data["/control-plane-protocols"] == "");
        REQUIRE(data["/interfaces"] == "");
        REQUIRE(data["/ribs"] == "");

        data = dataFromSysrepo(client, "/ietf-routing:routing/ribs/rib[name='ipv4-master']", SR_DS_OPERATIONAL);
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

            return 0ul;
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

        data = dataFromSysrepo(client, "/ietf-routing:routing/ribs/rib[name='ipv6-master']", SR_DS_OPERATIONAL);
        REQUIRE(data["/name"] == "ipv6-master");

        iproute2_exec_and_wait("route", "del", "198.51.100.0/24");
        iproute2_exec_and_wait("link", "set", IFACE, "down");
    }

    iproute2_exec_and_wait("link", "del", IFACE, "type", "dummy"); // Executed later again by ctest fixture cleanup just for sure. It remains here because of docopt sections: The interface needs to be setup again.
}
