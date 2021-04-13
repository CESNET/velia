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
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

#include <thread>
using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

const auto IFACE = "czechlight0"s;
const auto LINK_MAC = "50:40:30:20:10:00"s;
const auto LINK_MAC_CHANGED = "50:40:30:20:10:01"s;
const auto IPROUTE2_PATH = "/usr/bin/ip"s;
const auto SUDO_PATH = "/usr/bin/sudo"s;
const auto WAIT = 25ms;

template <class... Args>
void iproute2_run(const Args... args_)
{
    namespace bp = boost::process;
    auto logger = spdlog::get("main");

    bp::ipstream stdoutStream;
    bp::ipstream stderrStream;

    std::vector<std::string> args = {IPROUTE2_PATH, args_...};

    logger->trace("exec: {} {}", SUDO_PATH, boost::algorithm::join(args, " "));
    bp::child c(SUDO_PATH, boost::process::args = std::move(args), bp::std_out> stdoutStream, bp::std_err > stderrStream);
    c.wait();
    logger->trace("{} {} exited", SUDO_PATH, IPROUTE2_PATH);

    if (c.exit_code() != 0) {
        std::istreambuf_iterator<char> begin(stderrStream), end;
        std::string stderrOutput(begin, end);
        logger->critical("{} {} ended with a non-zero exit code. stderr: {}", SUDO_PATH, IPROUTE2_PATH, stderrOutput);

        throw std::runtime_error(SUDO_PATH + " " + IPROUTE2_PATH + " returned non-zero exit code " + std::to_string(c.exit_code()));
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
    iproute2_exec_and_wait("addr", "add", "10.9.8.7/24", "dev", IFACE);
    iproute2_exec_and_wait("addr", "add", "::ffff:a09:0807", "dev", IFACE);

    REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
        {"/ietf-ip:ipv4", ""},
        {"/ietf-ip:ipv4/address[ip='10.9.8.7']", ""},
        {"/ietf-ip:ipv4/address[ip='10.9.8.7']/ip", "10.9.8.7"},
        {"/ietf-ip:ipv4/address[ip='10.9.8.7']/prefix-length", "24"},
        {"/ietf-ip:ipv6", ""},
        {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']", ""},
        {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/ip", "::ffff:10.9.8.7"},
        {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/prefix-length", "128"},
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
        iproute2_exec_and_wait("link", "set", IFACE, "address", LINK_MAC_CHANGED);

        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/ietf-ip:ipv4", ""},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']", ""},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']/ip", "10.9.8.7"},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']/prefix-length", "24"},
                    {"/ietf-ip:ipv6", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/ip", "::ffff:10.9.8.7"},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/prefix-length", "128"},
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
        iproute2_exec_and_wait("addr", "add", "10.9.8.6/24", "dev", IFACE);
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/ietf-ip:ipv4", ""},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']", ""},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']/ip", "10.9.8.7"},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']/prefix-length", "24"},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.6']", ""},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.6']/ip", "10.9.8.6"},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.6']/prefix-length", "24"},
                    {"/ietf-ip:ipv6", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/ip", "::ffff:10.9.8.7"},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/prefix-length", "128"},
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

        iproute2_exec_and_wait("addr", "del", "10.9.8.6/24", "dev", IFACE);
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
                    {"/ietf-ip:ipv4", ""},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']", ""},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']/ip", "10.9.8.7"},
                    {"/ietf-ip:ipv4/address[ip='10.9.8.7']/prefix-length", "24"},
                    {"/ietf-ip:ipv6", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']", ""},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/ip", "::ffff:10.9.8.7"},
                    {"/ietf-ip:ipv6/address[ip='::ffff:10.9.8.7']/prefix-length", "128"},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements", ""},
                    {"/ietf-ip:ipv6/ietf-ipv6-unicast-routing:ipv6-router-advertisements/prefix-list", ""},
                    {"/name", IFACE},
                    {"/oper-status", "down"},
                    {"/phys-address", "50:40:30:20:10:00"},
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

    iproute2_exec_and_wait("link", "set", "dev", IFACE, "up");

    SECTION("Add and remove routes")
    {
        iproute2_exec_and_wait("link", "set", IFACE, "up");
        iproute2_exec_and_wait("route", "add", "10.9.0.0/16", "dev", IFACE);

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
            auto routeIdx = findRouteIndex("10.9.0.0/16");
            REQUIRE(routeIdx > 0);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/next-hop/outgoing-interface"] == IFACE);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/source-protocol"] == "ietf-routing:static");
        }
        {
            auto routeIdx = findRouteIndex("10.9.8.0/24");
            REQUIRE(routeIdx > 0);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/next-hop/outgoing-interface"] == IFACE);
            REQUIRE(data["/routes/route["s + std::to_string(routeIdx) + "]/source-protocol"] == "ietf-routing:direct");
        }

        data = dataFromSysrepo(client, "/ietf-routing:routing/ribs/rib[name='ipv6-master']", SR_DS_OPERATIONAL);
        REQUIRE(data["/name"] == "ipv6-master");

        iproute2_exec_and_wait("route", "del", "10.9.0.0/16");
        iproute2_exec_and_wait("link", "set", IFACE, "down");
    }

    iproute2_exec_and_wait("link", "delete", IFACE, "type", "dummy");
}
