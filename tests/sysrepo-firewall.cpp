/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "firewall/Firewall.h"
#include "test_log_setup.h"

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
add rule inet filter acls iif lo accept comment "Accept any localhost traffic"
)";

TEST_CASE("nftables generator")
{
    TEST_INIT_LOGS;
    auto srConn = std::make_shared<sysrepo::Connection>();
    auto srSess = std::make_shared<sysrepo::Session>(srConn);
    // Delete all acls at the start so we know what we're dealing with.
    srSess->delete_item("/ietf-access-control-list:acls");
    srSess->apply_changes(1000, 1);
    MockNft nft;

    REQUIRE_CALL(nft, consumeConfig(NFTABLES_OUTPUT_START));
    velia::firewall::SysrepoFirewall fw(srSess, [&nft] (const std::string& config) {nft.consumeConfig(config);});
    std::string inputData;
    std::string expectedOutput;

    SECTION("empty ACL start")
    {
        // Add an empty ACL
        {
            REQUIRE_CALL(nft, consumeConfig(NFTABLES_OUTPUT_START));
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/type", "mixed-eth-ipv4-ipv6-acl-type");
            srSess->apply_changes(1000, 1);
        }

        SECTION("add an IPv4 ACE")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls ip saddr 192.168.0.0/24 drop comment \"deny 192.168.0.0/24\"\n";
            srSess->set_item_str(
                    "/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']/matches/ipv4/source-ipv4-network",
                    "192.168.0.0/24");
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']/actions/forwarding", "drop");
        }

        SECTION("add an IPv6 ACE")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls ip6 saddr 2001:db8:85a3::8a2e:370:7334/128 accept comment \"deny an ipv6 address\"\n";
            srSess->set_item_str(
                    "/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny an ipv6 address']/matches/ipv6/source-ipv6-network",
                    "2001:0db8:85a3:0000:0000:8a2e:0370:7334/128");
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny an ipv6 address']/actions/forwarding", "accept");
        }

        SECTION("add ACE without 'matches'")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls drop comment \"drop eveything\"\n";
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='drop eveything']/actions/forwarding", "drop");
        }

        SECTION("add ACE with 'reject'")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls reject comment \"reject eveything\"\n";
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='reject eveything']/actions/forwarding", "reject");
        }

        SECTION("add ACE with 'reject'")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls reject comment \"reject eveything\"\n";
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='reject eveything']/actions/forwarding", "reject");
        }

        SECTION("add two ACEs")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls ip saddr 192.168.0.0/24 drop comment \"deny 192.168.0.0/24\"\n"
                "add rule inet filter acls reject comment \"reject eveything\"\n";
            srSess->set_item_str(
                    "/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']/matches/ipv4/source-ipv4-network",
                    "192.168.0.0/24");
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']/actions/forwarding", "drop");
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='reject eveything']/actions/forwarding", "reject");
        }

        REQUIRE_CALL(nft, consumeConfig(expectedOutput));
        srSess->apply_changes(1000, 1);

    }

    SECTION("non-empty ACL start")
    {
        // Add an non-empty ACL
        {
            REQUIRE_CALL(nft, consumeConfig(NFTABLES_OUTPUT_START +
                        "add rule inet filter acls ip saddr 192.168.0.0/24 drop comment \"deny 192.168.0.0/24\"\n"));
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/type", "mixed-eth-ipv4-ipv6-acl-type");
            srSess->set_item_str(
                    "/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']/matches/ipv4/source-ipv4-network",
                    "192.168.0.0/24");
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']/actions/forwarding", "drop");
            srSess->apply_changes(1000, 1);
        }

        SECTION("add another ACE")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls ip saddr 192.168.0.0/24 drop comment \"deny 192.168.0.0/24\"\n"
                "add rule inet filter acls ip saddr 192.168.13.0/24 drop comment \"also deny 192.168.13.0/24\"\n";
            srSess->set_item_str(
                    "/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='also deny 192.168.13.0/24']/matches/ipv4/source-ipv4-network",
                    "192.168.13.0/24");
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='also deny 192.168.13.0/24']/actions/forwarding", "drop");

        }

        SECTION("remove ACE")
        {
            expectedOutput = NFTABLES_OUTPUT_START;
            srSess->delete_item("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']");
        }

        SECTION("remove previous ACE and add another")
        {
            expectedOutput = NFTABLES_OUTPUT_START +
                "add rule inet filter acls ip saddr 192.168.13.0/24 drop comment \"deny 192.168.13.0/24\"\n";
            srSess->delete_item("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.0.0/24']");
            srSess->set_item_str(
                    "/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.13.0/24']/matches/ipv4/source-ipv4-network",
                    "192.168.13.0/24");
            srSess->set_item_str("/ietf-access-control-list:acls/acl[name='acls']/aces/ace[name='deny 192.168.13.0/24']/actions/forwarding", "drop");
        }

        SECTION("remove entire ACL")
        {
            expectedOutput = NFTABLES_OUTPUT_START;
            srSess->delete_item("/ietf-access-control-list:acls/acl[name='acls']");
        }

        REQUIRE_CALL(nft, consumeConfig(expectedOutput));
        srSess->apply_changes(1000, 1);
    }
}
