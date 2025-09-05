#include "trompeloeil_doctest.h"
#include <nlohmann/json.hpp>
#include "network/NetworkctlUtils.h"
#include "tests/configure.cmake.h"
#include "tests/pretty_printers.h"
#include "tests/test_log_setup.h"
#include "utils/io.h"

using namespace std::string_literals;

TEST_CASE("systemd-networkd managed links")
{
    TEST_SYSREPO_INIT_LOGS;

    nlohmann::json data;
    std::vector<std::string> expected;

    SECTION("Real device data")
    {
        data = velia::utils::readFileToString(CMAKE_CURRENT_SOURCE_DIR "/tests/networkctl/sdn-bidi-cplus1572-PGCL250303.json");
        expected = {"eth0", "eth1", "br0"};
    }

    SECTION("JSON sources")
    {
        nlohmann::json json;
        json["Interfaces"] = nlohmann::json::array();

        SECTION("All possible AdministrativeState values")
        {
            // systemd commit add1bc28d30bfb3ee2ccc804221a635cf188b733, file networkd-link.c, link_state_table
            for (const auto& [name, state] : {
                     std::pair<std::string, std::string>{"eth0", "pending"},
                     {"eth1", "initialized"},
                     {"eth2", "configuring"},
                     {"eth3", "configured"},
                     {"eth4", "unmanaged"},
                     {"eth5", "failed"},
                     {"eth6", "linger"},
                 }) {
                json["Interfaces"].push_back({{"Name", name}, {"AdministrativeState", state}});
            }

            expected = {"eth0", "eth1", "eth2", "eth3", "eth5", "eth6"};
        }

        SECTION("No interfaces")
        {
            expected = {};
        }

        SECTION("No managed interfaces")
        {
            for (const auto& name : {"eth0", "eth1", "lo"}) {
                json["Interfaces"].push_back({{"Name", name}, {"AdministrativeState", "unmanaged"}});
            }

            expected = {};
        }

        data = json.dump();
    }

    REQUIRE(velia::network::systemdNetworkdManagedLinks(data) == expected);
}

TEST_CASE("Active network configuration files")
{
    TEST_SYSREPO_INIT_LOGS;
    nlohmann::json data;
    std::map<std::string, velia::network::NetworkConfFiles> expected;
    std::set<std::string> managedLinks;

    SECTION("Real device data, no eth2 configuration in /run and /usr/lib")
    {
        data = velia::utils::readFileToString(CMAKE_CURRENT_SOURCE_DIR "/tests/networkctl/sdn-bidi-cplus1572-PGCL250303.json");
        managedLinks = {"eth0", "eth1", "eth2", "br0"};
        expected = {
            {"br0", {.networkFile = "/usr/lib/systemd/network/br0.network", .dropinFiles = {}}},
            {"eth0", {.networkFile = "/usr/lib/systemd/network/eth0.network", .dropinFiles = {}}},
            {"eth1", {.networkFile = "/usr/lib/systemd/network/eth1.network", .dropinFiles = {}}},
            {"eth2", {.networkFile = std::nullopt, .dropinFiles = {}}},
        };
    }

    SECTION("With dropins and no eth2 conf in /run")
    {
        data = velia::utils::readFileToString(CMAKE_CURRENT_SOURCE_DIR "/tests/networkctl/sdn-bidi-cplus1572-PGCL250305-with-dropins.json");
        managedLinks = {"eth0", "eth1", "eth2", "br0"};
        expected = {
            {"br0", {.networkFile = "/run/systemd/network/10-br0.network", .dropinFiles = {"/run/systemd/network/10-br0.network.d/lldp.conf"}}},
            {"eth0", {.networkFile = "/run/systemd/network/10-eth0.network", .dropinFiles = {}}},
            {"eth1", {.networkFile = "/run/systemd/network/10-eth1.network", .dropinFiles = {}}},
            {"eth2", {.networkFile = "/usr/lib/systemd/network/10-eth2.network", .dropinFiles = {}}},
        };
    }

    REQUIRE(velia::network::linkConfigurationFiles(data, managedLinks) == expected);

    REQUIRE_THROWS_WITH_AS(velia::network::linkConfigurationFiles(R"({"Interfaces": []})", {"eth0"}),
                           "Link eth0 not found in networkctl JSON data",
                           std::invalid_argument);
}
