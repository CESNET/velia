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
#include <netlink/route/addr.h>
#include <regex>
#include <sys/wait.h>
#include <thread>
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_vars.h"
#include "utils/exec.h"

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
void iproute2_exec_and_wait(const auto& wait, const Args... args_)
{
    iproute2_run(args_...);
    std::this_thread::sleep_for(wait); // wait for velia to process and publish the change
}


template <class T>
void nlCacheForeachWrapper(nl_cache* cache, std::function<void(T*)> cb)
{
    nl_cache_foreach(
        cache, [](nl_object* obj, void* data) {
            auto& cb = *static_cast<std::function<void(T*)>*>(data);
            auto link = reinterpret_cast<T*>(obj);
            cb(link);
        },
        &cb);
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

TEST_CASE("Test ietf-interfaces and ietf-routing")
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
