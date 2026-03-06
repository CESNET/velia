#include "trompeloeil_doctest.h"
#include <arpa/inet.h>
#include <libyang-cpp/Time.hpp>
#include <sdbus-c++/sdbus-c++.h>
#include "dbus-helpers/dbus_systemd_server.h"
#include "pretty_printers.h"
#include "system/IETFSystem.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/sysrepo-helpers/common.h"
#include "utils/io.h"

using namespace std::literals;

struct SystemctlMock {
    MAKE_CONST_MOCK0(reloadTimesync, void());
};

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

    DbusSystemdServer dbusServer(*dbusConnServer);
    SystemctlMock systemctlMock;

    velia::system::IETFSystem::SystemdConfigData resolve{
        .busName = dbusConnServer->getUniqueName(),
        .dropinDir = std::nullopt,
        .reload = std::nullopt,
    };
    velia::system::IETFSystem::SystemdConfigData timesync{
        .busName = dbusConnServer->getUniqueName(),
        .dropinDir = CMAKE_CURRENT_BINARY_DIR "tests/timesyncd.conf.d",
        .reload = [&systemctlMock] { systemctlMock.reloadTimesync(); },
    };
    velia::system::IETFSystem::SystemdConfigData timedate{
        .busName = dbusConnServer->getUniqueName(),
        .dropinDir = std::nullopt,
        .reload = std::nullopt,
    };

    SECTION("Test system-state")
    {
        static const auto modulePrefix = "/ietf-system:system-state"s;

        SECTION("Valid data")
        {
            std::filesystem::path osReleaseFile;
            std::filesystem::path procStatFile = CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.ok";
            std::map<std::string, std::string> expected;

            SECTION("Real data")
            {
                osReleaseFile = CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release";
                expected = {
                    {"/os-name", "CzechLight"},
                    {"/os-release", "v4-105-g8294175-dirty"},
                    {"/os-version", "v4-105-g8294175-dirty"},
                };
            }

            SECTION("Missing =")
            {
                osReleaseFile = CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-equal";
                expected = {
                    {"/os-name", ""},
                    {"/os-release", ""},
                    {"/os-version", ""},
                };
            }

            SECTION("Empty values")
            {
                osReleaseFile = CMAKE_CURRENT_SOURCE_DIR "/tests/system/empty-values";
                expected = {
                    {"/os-name", ""},
                    {"/os-release", ""},
                    {"/os-version", ""},
                };
            }

            REQUIRE_CALL(systemctlMock, reloadTimesync()).IN_SEQUENCE(seq1);
            auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess,
                                                                       osReleaseFile,
                                                                       procStatFile,
                                                                       *dbusConnClient,
                                                                       resolve,
                                                                       timesync,
                                                                       timedate);
            REQUIRE(dataFromSysrepo(client, modulePrefix + "/platform", sysrepo::Datastore::Operational) == expected);
        }

        SECTION("Invalid data (missing VERSION and NAME keys)")
        {
            // missing VERSION and NAME keys in os-release
            REQUIRE_THROWS_WITH_AS(std::make_shared<velia::system::IETFSystem>(srSess,
                                                                               CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-keys",
                                                                               CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.ok",
                                                                               *dbusConnClient,
                                                                               resolve,
                                                                               timesync,
                                                                               timedate),
                                   ("Could not read key NAME from file "s + CMAKE_CURRENT_SOURCE_DIR "/tests/system/missing-keys").c_str(),
                                   std::out_of_range);
        }
    }

    SECTION("dummy values")
    {
        REQUIRE_CALL(systemctlMock, reloadTimesync()).IN_SEQUENCE(seq1);
        auto sys = std::make_shared<velia::system::IETFSystem>(srSess,
                                                               CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release",
                                                               CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.ok",
                                                               *dbusConnClient,
                                                               resolve,
                                                               timesync,
                                                               timedate);
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
        // in its own scope, otherwise the tests below this will fail with something like:
        // Couldn't create RPC/action subscription: RPC subscription for "/ietf-system:system-restart" with priority 0 already exists.
        {
            REQUIRE_CALL(systemctlMock, reloadTimesync()).IN_SEQUENCE(seq1);
            auto sys = std::make_shared<velia::system::IETFSystem>(srSess,
                                                                   CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release",
                                                                   CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.ok",
                                                                   *dbusConnClient,
                                                                   resolve,
                                                                   timesync,
                                                                   timedate);
            client.switchDatastore(sysrepo::Datastore::Operational);
            REQUIRE(client.getData("/ietf-system:system-state/clock/current-datetime"));

            auto data = client.getData("/ietf-system:system-state/clock/boot-datetime");
            REQUIRE(data);
            auto bootTsNode = data->findPath("/ietf-system:system-state/clock/boot-datetime");
            REQUIRE(bootTsNode);

            // FIXME: HowardHinant/date unfortunately can't parse into utc_clock time_point, only full C++20 can do that
            auto bootTimePoint = std::chrono::clock_cast<std::chrono::utc_clock>(libyang::fromYangTimeFormat<std::chrono::system_clock>(bootTsNode->asTerm().valueStr()));
            REQUIRE(bootTimePoint == std::chrono::utc_time<std::chrono::utc_clock::duration>(std::chrono::seconds(1747993639)));
        }

        REQUIRE_THROWS_WITH_AS(std::make_shared<velia::system::IETFSystem>(srSess,
                                                                           CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release",
                                                                           CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.notfound",
                                                                           *dbusConnClient,
                                                                           resolve,
                                                                           timesync,
                                                                           timedate),
                               ("File '"s + CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.notfound' does not exist.").c_str(),
                               std::invalid_argument);

        REQUIRE_THROWS_WITH_AS(std::make_shared<velia::system::IETFSystem>(srSess,
                                                                           CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release",
                                                                           CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.no-btime",
                                                                           *dbusConnClient,
                                                                           resolve,
                                                                           timesync,
                                                                           timedate),
                               ("btime value not found in '"s + CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.no-btime'").c_str(),
                               std::runtime_error);

        REQUIRE_THROWS_WITH_AS(std::make_shared<velia::system::IETFSystem>(srSess,
                                                                           CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release",
                                                                           CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.invalid-btime",
                                                                           *dbusConnClient,
                                                                           resolve,
                                                                           timesync,
                                                                           timedate),
                               ("btime found in '"s + CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.invalid-btime' but could not be parsed (line was 'btime asd')").c_str(),
                               std::runtime_error);
    }

    SECTION("DNS resolvers")
    {
        REQUIRE_CALL(systemctlMock, reloadTimesync()).IN_SEQUENCE(seq1);
        auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess,
                                                                   CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release",
                                                                   CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.ok",
                                                                   *dbusConnClient,
                                                                   resolve,
                                                                   timesync,
                                                                   timedate);
        std::map<std::string, std::string> expected;

        dbusServer.setFallbackDNSEx({
            {0, AF_INET, {8, 8, 8, 8}, 0, "prvni.googlovsky.dns"},
            {0, AF_INET, {8, 8, 4, 4}, 0, "druhy.googlovsky.dns"},
            {2, AF_INET6, {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88}, 0, "this.was.in.my.resolved"},
        });

        SECTION("Both DNS and Fallback DNS") {
            dbusServer.setDNSEx({
                {0, AF_INET, {127, 0, 0, 1}, 0, "ahoj.com"},
                {2, AF_INET, {127, 0, 0, 1}, 444, "czech.light"},
                {2, AF_INET6, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 0, "idk.net"},
                {2, AF_INET6, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 153, "idk.net:153"},
            });

            expected = {
                {"/options", ""},
                {"/server[name='1']", ""},
                {"/server[name='1']/name", "1"},
                {"/server[name='1']/udp-and-tcp", ""},
                {"/server[name='1']/udp-and-tcp/address", "127.0.0.1"},
                {"/server[name='2']", ""},
                {"/server[name='2']/name", "2"},
                {"/server[name='2']/udp-and-tcp", ""},
                {"/server[name='2']/udp-and-tcp/address", "127.0.0.1"},
                {"/server[name='2']/udp-and-tcp/port", "444"},
                {"/server[name='3']", ""},
                {"/server[name='3']/name", "3"},
                {"/server[name='3']/udp-and-tcp", ""},
                {"/server[name='3']/udp-and-tcp/address", "::1"},
                {"/server[name='4']", ""},
                {"/server[name='4']/name", "4"},
                {"/server[name='4']/udp-and-tcp", ""},
                {"/server[name='4']/udp-and-tcp/address", "::1"},
                {"/server[name='4']/udp-and-tcp/port", "153"},
            };
        }

        SECTION("FallbackDNS only")
        {
            expected = {
                {"/options", ""},
                {"/server[name='1']", ""},
                {"/server[name='1']/name", "1"},
                {"/server[name='1']/udp-and-tcp", ""},
                {"/server[name='1']/udp-and-tcp/address", "8.8.8.8"},
                {"/server[name='2']", ""},
                {"/server[name='2']/name", "2"},
                {"/server[name='2']/udp-and-tcp", ""},
                {"/server[name='2']/udp-and-tcp/address", "8.8.4.4"},
                {"/server[name='3']", ""},
                {"/server[name='3']/name", "3"},
                {"/server[name='3']/udp-and-tcp", ""},
                {"/server[name='3']/udp-and-tcp/address", "2001:4860:4860::8888"},
            };
        }

        REQUIRE(dataFromSysrepo(client, "/ietf-system:system/dns-resolver", sysrepo::Datastore::Operational) == expected);
    }

    SECTION("NTP")
    {
        std::map<std::string, std::string> expected;
        REQUIRE_CALL(systemctlMock, reloadTimesync()).IN_SEQUENCE(seq1);
        auto sysrepo = std::make_shared<velia::system::IETFSystem>(srSess,
                                                                   CMAKE_CURRENT_SOURCE_DIR "/tests/system/os-release",
                                                                   CMAKE_CURRENT_SOURCE_DIR "/tests/system/proc_stat.ok",
                                                                   *dbusConnClient,
                                                                   resolve,
                                                                   timesync,
                                                                   timedate);

        SECTION("NTP enabled with servers")
        {
            dbusServer.setNTP(true);
            dbusServer.setSystemNTPServers({"tik.cesnet.cz", "tak.cesnet.cz"});
            dbusServer.setLinkNTPServers({"2001:db8::1", "10.0.0.2", "10.0.0.3", "10.0.0.4", "10.0.0.5", "10.0.0.6", "10.0.0.7", "10.0.0.8"});
            dbusServer.setFallbackNTPServers({"0.arch.pool.ntp.org", "1.arch.pool.ntp.org"});

            expected = {
                {"/enabled", "true"},
                {"/server[name='01']", ""},
                {"/server[name='01']/name", "01"},
                {"/server[name='01']/udp", ""},
                {"/server[name='01']/udp/address", "tik.cesnet.cz"},
                {"/server[name='02']", ""},
                {"/server[name='02']/name", "02"},
                {"/server[name='02']/udp", ""},
                {"/server[name='02']/udp/address", "tak.cesnet.cz"},
                {"/server[name='03']", ""},
                {"/server[name='03']/name", "03"},
                {"/server[name='03']/udp", ""},
                {"/server[name='03']/udp/address", "2001:db8::1"},
                {"/server[name='04']", ""},
                {"/server[name='04']/name", "04"},
                {"/server[name='04']/udp", ""},
                {"/server[name='04']/udp/address", "10.0.0.2"},
                {"/server[name='05']", ""},
                {"/server[name='05']/name", "05"},
                {"/server[name='05']/udp", ""},
                {"/server[name='05']/udp/address", "10.0.0.3"},
                {"/server[name='06']", ""},
                {"/server[name='06']/name", "06"},
                {"/server[name='06']/udp", ""},
                {"/server[name='06']/udp/address", "10.0.0.4"},
                {"/server[name='07']", ""},
                {"/server[name='07']/name", "07"},
                {"/server[name='07']/udp", ""},
                {"/server[name='07']/udp/address", "10.0.0.5"},
                {"/server[name='08']", ""},
                {"/server[name='08']/name", "08"},
                {"/server[name='08']/udp", ""},
                {"/server[name='08']/udp/address", "10.0.0.6"},
                {"/server[name='09']", ""},
                {"/server[name='09']/name", "09"},
                {"/server[name='09']/udp", ""},
                {"/server[name='09']/udp/address", "10.0.0.7"},
                {"/server[name='10']", ""},
                {"/server[name='10']/name", "10"},
                {"/server[name='10']/udp", ""},
                {"/server[name='10']/udp/address", "10.0.0.8"},
            };
        }

        SECTION("NTP enabled but only fallback servers available")
        {
            dbusServer.setNTP(true);
            dbusServer.setSystemNTPServers({});
            dbusServer.setLinkNTPServers({});
            dbusServer.setFallbackNTPServers({"0.arch.pool.ntp.org", "1.arch.pool.ntp.org"});

            expected = {
                {"/enabled", "true"},
                {"/server[name='1']", ""},
                {"/server[name='1']/name", "1"},
                {"/server[name='1']/udp", ""},
                {"/server[name='1']/udp/address", "0.arch.pool.ntp.org"},
                {"/server[name='2']", ""},
                {"/server[name='2']/name", "2"},
                {"/server[name='2']/udp", ""},
                {"/server[name='2']/udp/address", "1.arch.pool.ntp.org"},
            };
        }

        SECTION("NTP disabled")
        {
            dbusServer.setNTP(false);

            expected = {
                {"/enabled", "false"},
            };
        }

        SECTION("Generating NTP config")
        {
            dbusServer.setNTP(true);
            dbusServer.setFallbackNTPServers({"0.arch.pool.ntp.org", "1.arch.pool.ntp.org"});

            srSess.switchDatastore(sysrepo::Datastore::Running);

            SECTION("No servers")
            {
                dbusServer.setSystemNTPServers({});

                srSess.applyChanges();
                REQUIRE(velia::utils::readFileToString(*timesync.dropinDir / "timesyncd.conf") == R"(# Autogenerated by velia. Do not edit.
[Time]
NTP=
)");
                expected = {
                    {"/enabled", "true"},
                    {"/server[name='1']", ""},
                    {"/server[name='1']/name", "1"},
                    {"/server[name='1']/udp", ""},
                    {"/server[name='1']/udp/address", "0.arch.pool.ntp.org"},
                    {"/server[name='2']", ""},
                    {"/server[name='2']/name", "2"},
                    {"/server[name='2']/udp", ""},
                    {"/server[name='2']/udp/address", "1.arch.pool.ntp.org"},
                };
            }

            SECTION("Some servers")
            {
                srSess.setItem("/ietf-system:system/ntp/server[name='1']/udp/address", "tik.cesnet.cz");
                srSess.setItem("/ietf-system:system/ntp/server[name='2']/udp/address", "195.113.144.201");
                srSess.setItem("/ietf-system:system/ntp/server[name='3']/udp/address", "2001:718:1:1::144:201");
                REQUIRE_CALL(systemctlMock, reloadTimesync()).IN_SEQUENCE(seq1);
                srSess.applyChanges();
                REQUIRE(velia::utils::readFileToString(*timesync.dropinDir / "timesyncd.conf") == R"(# Autogenerated by velia. Do not edit.
[Time]
NTP=tik.cesnet.cz 195.113.144.201 2001:718:1:1::144:201
)");

                // dbus will return these data as system ntp servers
                dbusServer.setSystemNTPServers({"195.113.144.201", "tik.cesnet.cz", "2001:718:1:1::144:201"});

                expected = {
                    {"/enabled", "true"},
                    {"/server[name='1']", ""},
                    {"/server[name='1']/name", "1"},
                    {"/server[name='1']/udp", ""},
                    {"/server[name='1']/udp/address", "195.113.144.201"},
                    {"/server[name='2']", ""},
                    {"/server[name='2']/name", "2"},
                    {"/server[name='2']/udp", ""},
                    {"/server[name='2']/udp/address", "tik.cesnet.cz"},
                    {"/server[name='3']", ""},
                    {"/server[name='3']/name", "3"},
                    {"/server[name='3']/udp", ""},
                    {"/server[name='3']/udp/address", "2001:718:1:1::144:201"},
                };
            }
        }

        REQUIRE(dataFromSysrepo(client, "/ietf-system:system/ntp", sysrepo::Datastore::Operational) == expected);
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
