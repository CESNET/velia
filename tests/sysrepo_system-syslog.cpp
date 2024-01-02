#include "trompeloeil_doctest.h"
#include <filesystem>
#include <sdbus-c++/sdbus-c++.h>
#include <sysrepo-cpp/utils/exception.hpp>
#include "dbus-helpers/dbus_systemd_server.h"
#include "system/Syslog.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "utils/io.h"

#define EXPECT_RESTART_UNIT REQUIRE_CALL(systemdExpectations, restartUnit("systemd-journal-upload.service", "replace")).IN_SEQUENCE(seq1);

using namespace std::literals;

struct SdMock {
    MAKE_CONST_MOCK2(restartUnit, void(const std::string&, const std::string&));
};

TEST_CASE("Sysrepo czechlight-system:syslog")
{
    trompeloeil::sequence seq1;
    SdMock systemdExpectations;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    client.sendRPC(client.getContext().newPath("/ietf-factory-default:factory-reset"));

    auto dbusConnServer = sdbus::createSessionBusConnection();
    auto dbusConnClient = sdbus::createSessionBusConnection();
    dbusConnServer->enterEventLoopAsync();
    dbusConnClient->enterEventLoopAsync();
    DbusSystemdServer dbusServer(*dbusConnServer, [&systemdExpectations](const std::string& unitName, const std::string& mode) { systemdExpectations.restartUnit(unitName, mode); });

    auto fakeEnvFile = std::filesystem::path{CMAKE_CURRENT_BINARY_DIR} / "tests/syslog/journald-remote";
    std::filesystem::remove(fakeEnvFile);
    std::filesystem::create_directory(fakeEnvFile.parent_path());

    std::unique_ptr<velia::system::Syslog> syslog;

    SECTION("Presence container")
    {
        client.setItem("/czechlight-system:syslog/journal-upload/url", "https://upload.service");
        client.applyChanges();
        REQUIRE(!std::filesystem::exists(fakeEnvFile));

        EXPECT_RESTART_UNIT;
        syslog = std::make_unique<velia::system::Syslog>(srConn, *dbusConnClient, dbusConnServer->getUniqueName(), fakeEnvFile);
        REQUIRE(std::filesystem::exists(fakeEnvFile));
        REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=https://upload.service\n");
        waitForCompletionAndBitMore(seq1);
    }

    SECTION("No presence container, no file ")
    {
        syslog = std::make_unique<velia::system::Syslog>(srConn, *dbusConnClient, dbusConnServer->getUniqueName(), fakeEnvFile);
        REQUIRE(!std::filesystem::exists(fakeEnvFile));
    }

    SECTION("No presence container, file exists")
    {
        std::ofstream ofs(fakeEnvFile);
        ofs << "DESTINATION=hello.world\n";

        EXPECT_RESTART_UNIT;
        syslog = std::make_unique<velia::system::Syslog>(srConn, *dbusConnClient, dbusConnServer->getUniqueName(), fakeEnvFile);
        REQUIRE(!std::filesystem::exists(fakeEnvFile));
        waitForCompletionAndBitMore(seq1);
    }

    EXPECT_RESTART_UNIT;
    client.setItem("/czechlight-system:syslog/journal-upload/url", "https://1.2.3.4:1234");
    client.applyChanges();
    REQUIRE(std::filesystem::exists(fakeEnvFile));
    REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=https://1.2.3.4:1234\n");

    EXPECT_RESTART_UNIT;
    client.setItem("/czechlight-system:syslog/journal-upload/url", "ahoj");
    client.applyChanges();
    REQUIRE(std::filesystem::exists(fakeEnvFile));
    REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=ahoj\n");

    client.deleteItem("/czechlight-system:syslog/journal-upload/url");
    REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
    client.discardChanges();

    EXPECT_RESTART_UNIT;
    client.deleteItem("/czechlight-system:syslog/journal-upload");
    client.applyChanges();
    REQUIRE(!std::filesystem::exists(fakeEnvFile));

    EXPECT_RESTART_UNIT;
    client.setItem("/czechlight-system:syslog/journal-upload/url", "journal.cesnet.cz");
    client.applyChanges();
    REQUIRE(std::filesystem::exists(fakeEnvFile));
    REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=journal.cesnet.cz\n");

    waitForCompletionAndBitMore(seq1);
}
