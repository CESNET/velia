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

int getSysctl(const std::string& key)
{
    auto std_out = velia::utils::execAndWait(spdlog::get("main"), SYSCTL_EXECUTABLE, {"-n", key}, "", {});
    return std::stoi(std_out);
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

std::vector<std::pair<std::string, int>> getLinkAddresses(const std::string& linkName, int af)
{
    std::unique_ptr<nl_sock, decltype([](nl_sock* ptr) { nl_socket_free(ptr); })> socket(nl_socket_alloc());
    std::unique_ptr<nl_cache, decltype([](nl_cache* ptr) { nl_cache_free(ptr); })> addrCache;

    REQUIRE(nl_connect(socket.get(), NETLINK_ROUTE) == 0);

    {
        nl_cache* tmp;
        REQUIRE(rtnl_addr_alloc_cache(socket.get(), &tmp) == 0);
        addrCache.reset(tmp);
    }

    std::vector<std::pair<std::string, int>> addresses;
    nlCacheForeachWrapper<rtnl_addr>(addrCache.get(), [&addresses, &linkName, &af](rtnl_addr* addr) {
        if (auto fam = rtnl_addr_get_family(addr); fam == af) {
            if (std::unique_ptr<rtnl_link, decltype([](rtnl_link* ptr) { rtnl_link_put(ptr); })> link(rtnl_addr_get_link(addr)); rtnl_link_get_name(link.get()) == linkName) {
                auto ip = rtnl_addr_get_local(addr);
                auto binaddr = nl_addr_get_binary_addr(ip);

                std::array<char, INET6_ADDRSTRLEN> buf{};
                addresses.push_back(std::make_pair(inet_ntop(fam, binaddr, buf.data(), buf.size()), rtnl_addr_get_prefixlen(addr)));

                spdlog::get("system")->error("czechlight0 {} {} {} {}", fam, addresses.back().first, addresses.back().second, rtnl_addr_flags2str(rtnl_addr_get_flags(addr), buf.data(), buf.size()));
            }
        }
    });

    return addresses;
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

    const auto SYSCTL_ACCEPT_RA_ALL = getSysctl("net.ipv6.conf.all.accept_ra");
    const auto SYSCTL_ACCEPT_RA_IFACE = getSysctl("net.ipv6.conf."s + IFACE + ".accept_ra");
    [[maybe_unused]] const auto SLAAC_ACTIVE = SYSCTL_ACCEPT_RA_ALL == 1 || (SYSCTL_ACCEPT_RA_IFACE == 1 && SYSCTL_ACCEPT_RA_ALL == 0); // could be overriden for this particular link
    const auto SLAAC_ADDRESSES = getLinkAddresses(IFACE, AF_INET6);

    std::this_thread::sleep_for(1s); // is this neccessary? How soon can we expect the address?

    // we can't have any addresses now if slaac disabled
    if (!SLAAC_ACTIVE) {
        REQUIRE(SLAAC_ADDRESSES.empty());
    }

    iproute2_exec_and_wait("addr", "add", "192.0.2.1/24", "dev", IFACE); // from TEST-NET-1 (RFC 5737)
    iproute2_exec_and_wait("addr", "add", "::ffff:192.0.2.1", "dev", IFACE);

    std::map<std::string, std::string> expected{
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
    };

    if (SLAAC_ACTIVE) {
        for (const auto& [addr, prefixlen] : SLAAC_ADDRESSES) {
            expected["/ietf-ip:ipv6/address[ip='" + addr + "']"] = "";
            expected["/ietf-ip:ipv6/address[ip='" + addr + "']/ip"] = addr;
            expected["/ietf-ip:ipv6/address[ip='" + addr + "']/prefix-length"] = std::to_string(prefixlen);
        }
    }

    SECTION("Change physical address")
    {
        const auto LINK_MAC_CHANGED = "02:44:44:44:44:44"s;

        iproute2_exec_and_wait("link", "set", IFACE, "address", LINK_MAC_CHANGED);

        std::map<std::string, std::string> exp = expected;
        exp["/phys-address"] = LINK_MAC_CHANGED;
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == exp);
    }

    SECTION("Add and remove IP addresses")
    {
        iproute2_exec_and_wait("addr", "add", "192.0.2.6/24", "dev", IFACE);
        std::map<std::string, std::string> exp = expected;
        exp["/ietf-ip:ipv4/address[ip='192.0.2.6']"] = "";
        exp["/ietf-ip:ipv4/address[ip='192.0.2.6']/ip"] = "192.0.2.6";
        exp["/ietf-ip:ipv4/address[ip='192.0.2.6']/prefix-length"] = "24";
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == exp);

        iproute2_exec_and_wait("addr", "del", "192.0.2.6/24", "dev", IFACE);
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == expected);
    }

    SECTION("IPv6 LL gained when device up")
    {
        iproute2_exec_and_wait("link", "set", "dev", IFACE, "up");
        std::this_thread::sleep_for(WAIT); // FIXME: why this needs more wait time? Usually fails without more time.

        std::map<std::string, std::string> exp = expected;
        exp["/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']"] = "";
        exp["/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']/ip"] = "fe80::2:2ff:fe02:202";
        exp["/ietf-ip:ipv6/address[ip='fe80::2:2ff:fe02:202']/prefix-length"] = "64";
        exp["/oper-status"] = "unknown";
        exp["/statistics/out-octets"] = "70";
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == exp);

        iproute2_exec_and_wait("link", "set", "dev", IFACE, "down");
        exp["/oper-status"] = "down";
        REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces/interface[name='" + IFACE + "']", SR_DS_OPERATIONAL) == exp);
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

    iproute2_exec_and_wait("link", "del", IFACE, "type", "dummy"); // Executed later again by ctest fixture cleanup just for sure. It remains here because of doctest sections: The interface needs to be setup again.
}
