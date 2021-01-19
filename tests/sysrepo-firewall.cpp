/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "firewall/Firewall.h"
#include "test_log_setup.h"

const std::string ACL_DEFINITION_START = R"(
{
  "ietf-access-control-list:acls": {
    "acl": [
      {
        "name": "acls",
        "type": "mixed-eth-ipv4-ipv6-acl-type",
        "aces": {
          "ace": [
)";

const auto ACL_DEFINITION_END = R"(
          ]
        }
      }
    ]
  }
}
)";

class MockNft {
public:
    MAKE_MOCK1(consumeConfig, void(const std::string&));
    void operator()(const std::string& config)
    {
        consumeConfig(config);
    }
};

const std::string NFTABLES_OUTPUT_START = R"(flush ruleset
add table inet filter
add chain inet filter acls { type filter hook input priority 0; }
add rule inet filter acls ct state established,related accept
)";

TEST_CASE("nftables generator")
{
    TEST_INIT_LOGS;
    auto srConn = std::make_shared<sysrepo::Connection>();
    auto srSess = std::make_shared<sysrepo::Session>(srConn);
    // Delete all acls at the start so that our subscription gets the first data.
    srSess->delete_item("/ietf-access-control-list:acls");
    srSess->apply_changes(1000, 1);
    MockNft nft;

    SysrepoFirewall fw(srSess, [&nft] (const std::string& config) {nft.consumeConfig(config);});
    std::string inputData;
    std::string expectedOutput;

    // This is a base ACL which we will apply. The reason I'm starting with a non-empty datastore is that it allows me
    // to test if the subscription always gets all the data (the starting data and also new data in the SECTIONs below).
    inputData = ACL_DEFINITION_START + R"(
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
        }
    )" + ACL_DEFINITION_END;

    expectedOutput = NFTABLES_OUTPUT_START +
        "add rule inet filter acls ip saddr 192.168.0.0/24 accept comment \"allow 192.168.0.X\"\n";

    auto tree = srSess->get_context()->parse_data_mem(inputData.c_str(), LYD_JSON, LYD_OPT_GET);
    REQUIRE(tree);

    REQUIRE_CALL(nft, consumeConfig(expectedOutput));

    srSess->edit_batch(tree, "replace");
    srSess->apply_changes(1000, 1);
    SECTION("remove an ACE")
    {
        expectedOutput = NFTABLES_OUTPUT_START;
        REQUIRE_CALL(nft, consumeConfig(expectedOutput));
        srSess->delete_item("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='allow 192.168.0.X']");
        srSess->apply_changes(1000, 1);
    }

    SECTION("add an ACE")
    {
        expectedOutput = NFTABLES_OUTPUT_START +
            "add rule inet filter acls ip saddr 192.168.0.0/24 accept comment \"allow 192.168.0.X\"\n"
            "add rule inet filter acls drop comment \"drop everything\"\n";
        REQUIRE_CALL(nft, consumeConfig(expectedOutput));
        srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='drop everything']", nullptr);
        srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='drop everything']/actions/forwarding", "drop");
        srSess->apply_changes(1000, 1);
    }

    SECTION("add ipv6 ACE")
    {
        expectedOutput = NFTABLES_OUTPUT_START +
            "add rule inet filter acls ip saddr 192.168.0.0/24 accept comment \"allow 192.168.0.X\"\n"
            "add rule inet filter acls ip6 saddr 2001:db8:85a3::8a2e:370:7334/128 drop comment \"ipv6 example\"\n";
        REQUIRE_CALL(nft, consumeConfig(expectedOutput));
        srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='ipv6 example']", nullptr);
        srSess->set_item_str(
                "/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='ipv6 example']/matches/ipv6/source-ipv6-network",
                "2001:0db8:85a3:0000:0000:8a2e:0370:7334/128");
        srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='ipv6 example']/actions/forwarding", "drop");
        srSess->apply_changes(1000, 1);
    }
}
