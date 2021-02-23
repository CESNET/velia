/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <linux/netdevice.h>
#include "pretty_printers.h"
#include "system/IETFInterfaces.h"
#include "tests/mock/system.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

TEST_CASE("ietf-interfaces")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    trompeloeil::sequence seq1;
    std::string expectedContent;
    auto fakeRtnetlink = std::make_shared<FakeRtnetlink>();

    std::vector<FakeRtnetlink::LinkInfo> l {
        {"lo", "00:00:00:00:00:00", IF_OPER_UNKNOWN},
        {"eth0", "ff:ff:00:00:00:00", IF_OPER_UP},
        {"eth1", "ff:ff:00:00:00:01", IF_OPER_DOWN},
        {"eth2", "ff:ff:00:00:00:02", IF_OPER_LOWERLAYERDOWN},
    };
    ALLOW_CALL(*fakeRtnetlink, links()).RETURN(l).IN_SEQUENCE(seq1);

    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess, fakeRtnetlink);

    REQUIRE(dataFromSysrepo(client, "/ietf-interfaces:interfaces", SR_DS_OPERATIONAL) == std::map<std::string, std::string>{
        {"/interface[name='eth0']", ""},
        {"/interface[name='eth0']/name", "eth0"},
        {"/interface[name='eth0']/type", "iana-if-type:ethernetCsmacd"},
        {"/interface[name='eth0']/phys-address", "ff:ff:00:00:00:00"},
        {"/interface[name='eth0']/oper-status", "up"},
        {"/interface[name='eth1']", ""},
        {"/interface[name='eth1']/name", "eth1"},
        {"/interface[name='eth1']/type", "iana-if-type:ethernetCsmacd"},
        {"/interface[name='eth1']/phys-address", "ff:ff:00:00:00:01"},
        {"/interface[name='eth1']/oper-status", "down"},
        {"/interface[name='eth2']", ""},
        {"/interface[name='eth2']/name", "eth2"},
        {"/interface[name='eth2']/type", "iana-if-type:ethernetCsmacd"},
        {"/interface[name='eth2']/phys-address", "ff:ff:00:00:00:02"},
        {"/interface[name='eth2']/oper-status", "lower-layer-down"},
        {"/interface[name='lo']", ""},
        {"/interface[name='lo']/name", "lo"},
        {"/interface[name='lo']/type", "iana-if-type:ethernetCsmacd"},
        {"/interface[name='lo']/phys-address", "00:00:00:00:00:00"},
        {"/interface[name='lo']/oper-status", "unknown"},
    });
}

#define REAL_SYSTEM_TEST
#ifdef REAL_SYSTEM_TEST
TEST_CASE("ietf-interfaces on live system")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    auto network = std::make_shared<velia::system::IETFInterfaces>(srSess, std::make_shared<velia::system::Rtnetlink>());

    for (const auto& [k, v] : dataFromSysrepo(client, "/ietf-interfaces:interfaces", SR_DS_OPERATIONAL)) {
        spdlog::get("main")->info("{}: {}", k, v);
    }
}
#endif
