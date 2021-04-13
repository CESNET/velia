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
#include <sys/wait.h>
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

#include <thread>
using namespace std::chrono_literals;
using namespace std::string_literals;

template <class... Args>
void sudo_run(const Args... args_)
{
    namespace bp = boost::process;
    auto logger = spdlog::get("main");

    bp::pipe stdinPipe;
    bp::ipstream stdoutStream;
    bp::ipstream stderrStream;

    auto absolutePath = "/usr/bin/sudo"s;
    std::vector<std::string> args = {args_...};

    logger->trace("exec: {} {}", absolutePath, boost::algorithm::join(args, " "));
    bp::child c(absolutePath, boost::process::args = std::move(args), bp::std_in<stdinPipe, bp::std_out> stdoutStream, bp::std_err > stderrStream);
    stdinPipe.close();

    c.wait();
    logger->trace("{} exited", absolutePath);

    if (c.exit_code()) {
        std::istreambuf_iterator<char> begin(stderrStream), end;
        std::string stderrOutput(begin, end);
        logger->critical("{} ended with a non-zero exit code. stderr: {}", absolutePath, stderrOutput);

        throw std::runtime_error(absolutePath + " returned non-zero exit code " + std::to_string(c.exit_code()));
    }
}

auto IFACE = "czechlight9"s;

TEST_CASE("ietf-interfaces localhost")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;

    TEST_SYSREPO_INIT_CLIENT;

    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess);

#if 0
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
#endif

    sudo_run("/usr/bin/ip", "link", "add", IFACE, "address", "50:40:30:20:10:00", "type", "dummy");
    sudo_run("/usr/bin/ip", "addr", "add", "10.9.8.7/24", "dev", IFACE);
    sudo_run("/usr/bin/ip", "addr", "add", "::ffff:a09:0807", "dev", IFACE);

    std::this_thread::sleep_for(50ms);
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
        {"/name", "czechlight9"},
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

    SECTION("Change physical address")
    {
        sudo_run("/usr/bin/ip", "link", "set", IFACE, "address", "50:40:30:20:10:01");

        std::this_thread::sleep_for(50ms);
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
                    {"/name", "czechlight9"},
                    {"/oper-status", "down"},
                    {"/phys-address", "50:40:30:20:10:01"},
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
        sudo_run("/usr/bin/ip", "addr", "add", "10.9.8.6/24", "dev", IFACE);
        std::this_thread::sleep_for(50ms);
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
                    {"/name", "czechlight9"},
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

        sudo_run("/usr/bin/ip", "addr", "del", "10.9.8.6/24", "dev", IFACE);
        std::this_thread::sleep_for(50ms);
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
                    {"/name", "czechlight9"},
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

    sudo_run("/usr/bin/ip", "link", "set", "dev", IFACE, "up");
    std::this_thread::sleep_for(50ms);

    SECTION("Add and remove routes")
    {
        sudo_run("/usr/bin/ip", "route", "add", "10.9.0.0/16", "dev", IFACE);
        std::this_thread::sleep_for(50ms);
        CHECK(dataFromSysrepo(client, "/ietf-routing:routing", SR_DS_OPERATIONAL) == std::map<std::string, std::string> {
            {"/rib[name='ipv4-master']/routes/route[1]/ietf-ipv4-unicast-routing:destination-prefix", "0.0.0.0/0"},
            {"/rib[name='ipv4-master']/routes/route[1]/next-hop/ietf-ipv4-unicast-routing:next-hop-address", "10.88.3.1"},
            {"/rib[name='ipv4-master']/routes/route[1]/next-hop/outgoing-interface", "wlp3s0"},
            {"/rib[name='ipv4-master']/routes/route[1]/source-protocol", "czechlight-network:dhcp"},
            {"/rib[name='ipv4-master']/routes/route[2]/ietf-ipv4-unicast-routing:destination-prefix", "10.9.8.0/24"},
            {"/rib[name='ipv4-master']/routes/route[2]/next-hop/outgoing-interface", "czechlight9"},
            {"/rib[name='ipv4-master']/routes/route[2]/source-protocol", "direct"},
            {"/rib[name='ipv4-master']/routes/route[3]/ietf-ipv4-unicast-routing:destination-prefix", "10.88.3.0/24"},
            {"/rib[name='ipv4-master']/routes/route[3]/next-hop/outgoing-interface", "wlp3s0"},
            {"/rib[name='ipv4-master']/routes/route[3]/source-protocol", "direct"},
            {"/rib[name='ipv6-master']/routes/route[1]/ietf-ipv6-unicast-routing:destination-prefix", "::1/128"},
            {"/rib[name='ipv6-master']/routes/route[1]/next-hop/outgoing-interface", "lo"},
            {"/rib[name='ipv6-master']/routes/route[1]/source-protocol", "static"},
            {"/rib[name='ipv6-master']/routes/route[2]/ietf-ipv6-unicast-routing:destination-prefix", "::ffff:10.9.8.7/128"},
            {"/rib[name='ipv6-master']/routes/route[2]/next-hop/outgoing-interface", "czechlight9"},
            {"/rib[name='ipv6-master']/routes/route[2]/source-protocol", "static"},
            {"/rib[name='ipv6-master']/routes/route[3]/ietf-ipv6-unicast-routing:destination-prefix", "fe80::/64"},
            {"/rib[name='ipv6-master']/routes/route[3]/next-hop/outgoing-interface", "czechlight9"},
            {"/rib[name='ipv6-master']/routes/route[3]/source-protocol", "static"},
            {"/rib[name='ipv6-master']/routes/route[4]/ietf-ipv6-unicast-routing:destination-prefix", "fe80::/64"},
            {"/rib[name='ipv6-master']/routes/route[4]/next-hop/outgoing-interface", "wlp3s0"},
            {"/rib[name='ipv6-master']/routes/route[4]/source-protocol", "static"},
        });

        sudo_run("/usr/bin/ip", "route", "del", "10.9.0.0/16");
    }

    sudo_run("/usr/bin/ip", "link", "delete", IFACE, "type", "dummy");
}
