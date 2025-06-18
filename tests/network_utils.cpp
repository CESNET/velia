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
