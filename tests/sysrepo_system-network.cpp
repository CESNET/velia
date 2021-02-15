/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "fs-helpers/utils.h"
#include "pretty_printers.h"
#include "system/Network.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "utils/io.h"

using namespace std::string_literals;
using namespace std::chrono_literals;

struct FakeNetworkReload {
public:
    MAKE_CONST_MOCK1(cb, void(const std::vector<std::string>&));
};

TEST_CASE("Standalone eth1")
{
    const auto PRESENCE_CONTAINER = "/czechlight-system:networking/standalone-eth1";

    static const std::string EXPECTED_CONTENT_BRIDGE = R"([Match]
Name=eth1

[Network]
Bridge=br0
LLDP=true
EmitLLDP=nearest-bridge
)";

    static const std::string EXPECTED_CONTENT_DHCP = R"([Match]
Name=eth1

[Network]
DHCP=yes
LLDP=true
EmitLLDP=nearest-bridge
)";

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    trompeloeil::sequence seq1;
    std::string expectedContent;
    auto fake = FakeNetworkReload();
    auto fakeRunDir = std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "tests/network/run"s;
    auto expectedFilePath = fakeRunDir / "eth1.network"s;

    // reset
    removeDirectoryTreeIfExists(fakeRunDir);
    std::filesystem::create_directories(fakeRunDir);

    SECTION("Container not present. Start with bridge configuration")
    {
        client->delete_item(PRESENCE_CONTAINER);
        client->apply_changes();

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth1"})).IN_SEQUENCE(seq1);
        auto network = std::make_shared<velia::system::Network>(srConn, fakeRunDir, [&fake](const std::vector<std::string>& updatedInterfaces) { fake.cb(updatedInterfaces); });

        waitForCompletionAndBitMore(seq1);

        REQUIRE(std::filesystem::exists(expectedFilePath));
        REQUIRE(velia::utils::readFileToString(expectedFilePath) == EXPECTED_CONTENT_BRIDGE);

        SECTION("Nothing happens")
        {

        }
        SECTION("Change: Container present. Switch to DHCP configuration")
        {
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth1"})).IN_SEQUENCE(seq1);

            client->set_item(PRESENCE_CONTAINER);
            client->apply_changes();
            waitForCompletionAndBitMore(seq1);

            REQUIRE(std::filesystem::exists(expectedFilePath));
            REQUIRE(velia::utils::readFileToString(expectedFilePath) == EXPECTED_CONTENT_DHCP);
        }
    }
    SECTION("Container present. Start with DHCP configuration")
    {
        client->set_item(PRESENCE_CONTAINER);
        client->apply_changes();

        REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth1"})).IN_SEQUENCE(seq1);
        auto network = std::make_shared<velia::system::Network>(srConn, fakeRunDir, [&fake](const std::vector<std::string>& updatedInterfaces) { fake.cb(updatedInterfaces); });

        waitForCompletionAndBitMore(seq1);

        REQUIRE(std::filesystem::exists(expectedFilePath));
        REQUIRE(velia::utils::readFileToString(expectedFilePath) == EXPECTED_CONTENT_DHCP);

        SECTION("Nothing happens")
        {

        }
        SECTION("Change: Container not present. Switch to bridge configuration")
        {
            REQUIRE_CALL(fake, cb(std::vector<std::string>{"eth1"})).IN_SEQUENCE(seq1);

            client->delete_item(PRESENCE_CONTAINER);
            client->apply_changes();
            waitForCompletionAndBitMore(seq1);

            REQUIRE(std::filesystem::exists(expectedFilePath));
            REQUIRE(velia::utils::readFileToString(expectedFilePath) == EXPECTED_CONTENT_BRIDGE);
        }
    }
}
