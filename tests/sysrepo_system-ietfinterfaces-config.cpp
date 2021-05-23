/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "pretty_printers.h"
#include "system/IETFInterfacesConfig.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "tests/mock/system.h"
#include "utils/io.h"

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

struct FakeNetworkReload {
public:
    MAKE_CONST_MOCK1(cb, void(const std::vector<std::string>&));
};

using namespace std::string_literals;

TEST_CASE("changes in ietf-interfaces")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    trompeloeil::sequence seq1;

    auto fake = FakeNetworkReload();
    auto fakeConfigDir = std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "tests/network/"s;
    auto expectedFilePath = fakeConfigDir / "lo.network";

    srSess->session_switch_ds(SR_DS_RUNNING);
    client->session_switch_ds(SR_DS_RUNNING);

    auto network = std::make_shared<velia::system::IETFInterfacesConfig>(srSess, fakeConfigDir, std::vector<std::string>{"lo", "eth0"}, [&fake](const std::vector<std::string>& updatedInterfaces) { fake.cb(updatedInterfaces); });

    SECTION("Setting IPs to interfaces")
    {
        std::string expectedContents;

        client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/type", "iana-if-type:softwareLoopback");

        SECTION("Single IPv4 address")
        {
            client->set_item_str("/ietf-interfaces:interfaces/interface[name='lo']/ietf-ip:ipv4/ietf-ip:address[ip='127.0.0.1']/ietf-ip:prefix-length", "8");
            expectedContents = CONFIG["Address=127.0.0.1/8"];
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

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"lo"})).IN_SEQUENCE(seq1);
        client->apply_changes();
        REQUIRE(std::filesystem::exists(expectedFilePath));
        REQUIRE(velia::utils::readFileToString(expectedFilePath) == expectedContents);

        // reset the contents
        client->delete_item("/ietf-interfaces:interfaces/interface[name='lo']");
        REQUIRE_CALL(fake, cb(std::vector<std::string>{"lo"})).IN_SEQUENCE(seq1);
        client->apply_changes();
    }
}
