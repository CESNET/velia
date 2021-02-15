/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "fs-helpers/FileInjector.h"
#include "pretty_printers.h"
#include "system/Network.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "utils/io.h"
#include <thread>

using namespace std::string_literals;
using namespace std::chrono_literals;

#if 0
TEST_CASE("Network")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    static const auto EXPECTED_CONTENTS_BRIDGE = "[Match]\n"
                                                 "Name=eth1\n\n"
                                                 "[Network]\n"
                                                 "Bridge=br0\n"
                                                 "LLDP=true\n"
                                                 "EmitLLDP=nearest-bridge\n";

    static const auto EXPECTED_CONTENTS_DHCP = "[Match]\n"
                                               "Name=eth1\n\n"
                                               "[Network]\n"
                                               "DHCP=yes\n"
                                               "LLDP=true\n"
                                               "EmitLLDP=nearest-bridge\n";

    static const auto EXPECTED_CONTENTS_STATIC_IP = "[Match]\n"
                                                    "Name=eth1\n\n"
                                                    "[Network]\n"
                                                    "Address=127.0.0.1\n"
                                                    "LLDP=true\n"
                                                    "EmitLLDP=nearest-bridge\n";

    static const auto FILEPATH = std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "eth1.network";

    std::string expectedContent = EXPECTED_CONTENTS_BRIDGE;
    SECTION("Start with static address")
    {
        client->set_item_str("/czechlight-system:networking/eth1-ip", "127.0.0.1");
        expectedContent = EXPECTED_CONTENTS_STATIC_IP;
    }
    SECTION("Start with DHCP")
    {
        client->set_item("/czechlight-system:networking/eth1-ip");
        client->delete_item("/czechlight-system:networking/eth1-ip/static-ip-address");
        expectedContent = EXPECTED_CONTENTS_DHCP;
    }

    auto file = FileInjector(FILEPATH, std::filesystem::perms::all, "");
    auto network = std::make_shared<velia::system::Network>(srConn, CMAKE_CURRENT_BINARY_DIR, CMAKE_CURRENT_BINARY_DIR);

    SECTION("Nothing happens")
    {
        REQUIRE(std::filesystem::exists(FILEPATH));
        REQUIRE(velia::utils::readFileToString(FILEPATH) == expectedContent);
    }
    SECTION("Client changes the network subtree")
    {
        SECTION("Static")
        {
            client->set_item_str("/czechlight-system:networking/eth1-ip/static-ip-address", "127.0.0.1");
            expectedContent = EXPECTED_CONTENTS_STATIC_IP;
        }
        SECTION("DHCP")
        {
            client->set_item("/czechlight-system:networking/eth1-ip");
            client->delete_item("/czechlight-system:networking/eth1-ip/static-ip-address");
            expectedContent = EXPECTED_CONTENTS_DHCP;
        }
        SECTION("Presence container deleted")
        {
            client->delete_item("/czechlight-system:networking/eth1-ip");
            expectedContent = EXPECTED_CONTENTS_BRIDGE;
        }

        client->apply_changes();
        std::this_thread::sleep_for(10ms);

        REQUIRE(std::filesystem::exists(FILEPATH));
        REQUIRE(velia::utils::readFileToString(FILEPATH) == expectedContent);
    }
}
#endif