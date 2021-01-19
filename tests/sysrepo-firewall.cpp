/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "firewall/Firewall.h"

TEST_CASE("nftables generator")
{
    auto srConn = std::make_shared<sysrepo::Connection>();
    auto srSess = std::make_shared<sysrepo::Session>(srConn);
    const auto& lyCtx = srSess->get_context();
    std::string inputData;
    std::string expectedOutput;

    SECTION("very simple")
    {
        inputData = R"(
{
  "ietf-access-control-list:acls": {
    "acl": [
      {
        "name": "main",
        "type": "mixed-eth-ipv4-ipv6-acl-type",
        "aces": {
          "ace": [
            {
              "name": "allow 192.168.0.X",
              "matches": {
                "ipv4": {
                  "source-ipv4-network": "192.168.0.0/24"
                }
              },
              "actions": {
                "forwarding": "accept"
              }
            },
            {
              "name": "deny everything",
              "actions": {
                "forwarding": "drop"
              }
            }
          ]
        }
      }
    ]
  }
}
        )";

        expectedOutput = R"(flush ruleset
add table inet filter
add chain inet filter main { type filter hook input priority 0; }
add rule inet filter main ct state established,related accept
add rule inet filter main ip saddr 192.168.0.0/24 accept
add rule inet filter main drop
)";
    }



    auto tree = lyCtx->parse_data_mem(inputData.c_str(), LYD_JSON, LYD_OPT_GET);
    REQUIRE(tree);
    REQUIRE(generateNftConfig(tree) == expectedOutput);
}
