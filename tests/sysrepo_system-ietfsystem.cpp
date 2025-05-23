#include "trompeloeil_doctest.h"
#include <arpa/inet.h>
#include <sdbus-c++/sdbus-c++.h>
#include "dbus-helpers/dbus_resolve1_server.h"
#include "pretty_printers.h"
#include "system/IETFSystem.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/sysrepo-helpers/common.h"

using namespace std::literals;

TEST_CASE("Sysrepo ietf-system")
{
    trompeloeil::sequence seq1;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    auto dbusConnServer = sdbus::createSessionBusConnection();
    auto dbusConnClient = sdbus::createSessionBusConnection();

    dbusConnServer->enterEventLoopAsync();
    dbusConnClient->enterEventLoopAsync();

    DbusResolve1Server dbusServer(*dbusConnServer);

    SECTION("Test system-state")
    {
        static const auto modulePrefix = "/ietf-system:system-state"s;

        SECTION("Valid data")
        {
            std::filesystem::path file;
            std::map<std::string, std::string> expected;

            SECTION("Real data")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release";
                expected = {
                    {"/os-name", "CzechLight"},
                    {"/os-release", "v4-105-g8294175-dirty"},
                    {"/os-version", "v4-105-g8294175-dirty"},
                };
            }

            SECTION("Missing =")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-equal";
                expected = {
                    {"/os-name", ""},
                    {"/os-release", ""},
                    {"/os-version", ""},
                };
            }

            SECTION("Empty values")
            {
                file = CMAKE_CURRENT_SOURCE_DIR "/tests/system/empty-values";
                expected = {
                    {"/os-name", ""},
                    {"/os-release", ""},
                    {"/os-version", ""},
                };
            }

            auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess, file, *dbusConnClient, dbusConnServer->getUniqueName());
            REQUIRE(dataFromSysrepo(client, modulePrefix + "/platform", sysrepo::Datastore::Operational) == expected);
        }

        SECTION("Invalid data (missing VERSION and NAME keys)")
        {
            REQUIRE_THROWS_WITH_AS(std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-keys", *dbusConnClient, dbusConnServer->getUniqueName()),
                                   "Could not read key NAME from file /home/tomas/zdrojaky/cesnet/velia/tests/system/missing-keys",
                                   std::out_of_range);
        }
    }

    SECTION("dummy values")
    {
        auto sys = std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release", *dbusConnClient, dbusConnServer->getUniqueName());
        const char* xpath;

        SECTION("location") {
            xpath = "/ietf-system:system/location";
        }

        SECTION("contact") {
            xpath = "/ietf-system:system/contact";
        }

        client.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(!client.getData(xpath));

        client.switchDatastore(sysrepo::Datastore::Running);
        client.setItem(xpath, "lamparna");

        REQUIRE(client.getData(xpath));
    }

    SECTION("clock")
    {
        auto sys = std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release", *dbusConnClient, dbusConnServer->getUniqueName());
        client.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(client.getData("/ietf-system:system-state/clock/current-datetime"));
    }

    SECTION("DNS resolvers")
    {
        auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release", *dbusConnClient, dbusConnServer->getUniqueName());
        std::map<std::string, std::string> expected;

        dbusServer.setFallbackDNSEx({
            {0, AF_INET, {8, 8, 8, 8}, 0, "prvni.googlovsky.dns"},
            {0, AF_INET, {8, 8, 4, 4}, 0, "druhy.googlovsky.dns"},
            {2, AF_INET6, {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88}, 0, "this.was.in.my.resolved"},
        });

        SECTION("Both DNS and Fallback DNS") {
            dbusServer.setDNSEx({
                {0, AF_INET, {127, 0, 0, 1}, 0, "ahoj.com"},
                {2, AF_INET, {127, 0, 0, 1}, 0, "czech.light"},
                {2, AF_INET6, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 53, "idk.net"},
            });

            expected = {
                {"/options", ""},
                {"/server[name='127.0.0.1']", ""},
                {"/server[name='127.0.0.1']/name", "127.0.0.1"},
                {"/server[name='127.0.0.1']/udp-and-tcp", ""},
                {"/server[name='127.0.0.1']/udp-and-tcp/address", "127.0.0.1"},
                {"/server[name='::1']", ""},
                {"/server[name='::1']/name", "::1"},
                {"/server[name='::1']/udp-and-tcp", ""},
                {"/server[name='::1']/udp-and-tcp/address", "::1"},
            };
        }

        SECTION("FallbackDNS only")
        {
            expected = {
                {"/options", ""},
                {"/server[name='2001:4860:4860::8888']", ""},
                {"/server[name='2001:4860:4860::8888']/name", "2001:4860:4860::8888"},
                {"/server[name='2001:4860:4860::8888']/udp-and-tcp", ""},
                {"/server[name='2001:4860:4860::8888']/udp-and-tcp/address", "2001:4860:4860::8888"},
                {"/server[name='8.8.4.4']", ""},
                {"/server[name='8.8.4.4']/name", "8.8.4.4"},
                {"/server[name='8.8.4.4']/udp-and-tcp", ""},
                {"/server[name='8.8.4.4']/udp-and-tcp/address", "8.8.4.4"},
                {"/server[name='8.8.8.8']", ""},
                {"/server[name='8.8.8.8']/name", "8.8.8.8"},
                {"/server[name='8.8.8.8']/udp-and-tcp", ""},
                {"/server[name='8.8.8.8']/udp-and-tcp/address", "8.8.8.8"},
            };
        }

        REQUIRE(dataFromSysrepo(client, "/ietf-system:system/dns-resolver", sysrepo::Datastore::Operational) == expected);
    }

#ifdef TEST_RPC_SYSTEM_REBOOT
    SECTION("RPC system-restart")
    {
        auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess, CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release", dbusConnection);

        auto rpcInput = std::make_shared<sysrepo::Vals>(0);
        auto res = client->rpc_send("/ietf-system:system-restart", rpcInput);
        REQUIRE(res->val_cnt() == 0);
    }
#endif
}
