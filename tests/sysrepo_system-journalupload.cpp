#include "trompeloeil_doctest.h"
#include <filesystem>
#include <sysrepo-cpp/utils/exception.hpp>
#include "system/JournalUpload.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "utils/io.h"

#define EXPECT_RESTART_UNIT REQUIRE_CALL(restartMock, restartCalled()).IN_SEQUENCE(seq1);

using namespace std::literals;

struct SdManagerMock {
    MAKE_CONST_MOCK2(restartUnit, void(const std::string&, const std::string&));
};

struct RestartMock {
    MAKE_CONST_MOCK0(restartCalled, void());
};

TEST_CASE("Sysrepo czechlight-system:syslog")
{
    trompeloeil::sequence seq1;
    RestartMock restartMock;

    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    client.sendRPC(client.getContext().newPath("/ietf-factory-default:factory-reset"));

    auto fakeEnvFile = std::filesystem::path{CMAKE_CURRENT_BINARY_DIR} / "tests/journal-upload/env";
    std::filesystem::remove(fakeEnvFile);
    std::filesystem::create_directory(fakeEnvFile.parent_path());

    std::unique_ptr<velia::system::JournalUpload> syslog;

    auto restartCb = [&restartMock](auto) {
        restartMock.restartCalled();
    };

    SECTION("Presence container")
    {
        client.setItem("/czechlight-system:journal-upload/host", "upload.example.com");
        client.applyChanges();
        REQUIRE(!std::filesystem::exists(fakeEnvFile));

        EXPECT_RESTART_UNIT;
        syslog = std::make_unique<velia::system::JournalUpload>(srSess, fakeEnvFile, restartCb);
        waitForCompletionAndBitMore(seq1);
        REQUIRE(std::filesystem::exists(fakeEnvFile));
        REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=https://upload.example.com:19532\n");
    }

    SECTION("No presence container, no file")
    {
        syslog = std::make_unique<velia::system::JournalUpload>(srSess, fakeEnvFile, restartCb);
    }

    SECTION("No presence container, file exists")
    {
        std::ofstream ofs(fakeEnvFile);
        ofs << "DESTINATION=192.0.2.254\n";

        EXPECT_RESTART_UNIT;
        syslog = std::make_unique<velia::system::JournalUpload>(srSess, fakeEnvFile, restartCb);
        waitForCompletionAndBitMore(seq1);
        REQUIRE(!std::filesystem::exists(fakeEnvFile));
    }

    EXPECT_RESTART_UNIT;
    client.setItem("/czechlight-system:journal-upload/protocol", "http");
    client.setItem("/czechlight-system:journal-upload/port", "1234");
    client.setItem("/czechlight-system:journal-upload/host", "192.0.2.111");
    client.applyChanges();
    waitForCompletionAndBitMore(seq1);
    REQUIRE(std::filesystem::exists(fakeEnvFile));
    REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=http://192.0.2.111:1234\n");

    EXPECT_RESTART_UNIT;
    client.setItem("/czechlight-system:journal-upload/host", "example.net");
    client.deleteItem("/czechlight-system:journal-upload/protocol");
    client.deleteItem("/czechlight-system:journal-upload/port");
    client.applyChanges();
    waitForCompletionAndBitMore(seq1);
    REQUIRE(std::filesystem::exists(fakeEnvFile));
    REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=https://example.net:19532\n");

    EXPECT_RESTART_UNIT;
    client.deleteItem("/czechlight-system:journal-upload");
    client.applyChanges();
    waitForCompletionAndBitMore(seq1);
    REQUIRE(!std::filesystem::exists(fakeEnvFile));

    EXPECT_RESTART_UNIT;
    client.setItem("/czechlight-system:journal-upload/host", "192.0.2.2");
    client.applyChanges();
    waitForCompletionAndBitMore(seq1);
    REQUIRE(std::filesystem::exists(fakeEnvFile));
    REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=https://192.0.2.2:19532\n");

    // update to same value does not trigger restart
    client.setItem("/czechlight-system:journal-upload/host", "192.0.2.2");
    client.applyChanges();
    waitForCompletionAndBitMore(seq1);
    REQUIRE(std::filesystem::exists(fakeEnvFile));
    REQUIRE(velia::utils::readFileToString(fakeEnvFile) == "DESTINATION=https://192.0.2.2:19532\n");

    // host leaf is mandatory
    client.deleteItem("/czechlight-system:journal-upload/host");
    REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
    client.discardChanges();

    // invalid protocol value
    REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/protocol", "imap"), sysrepo::ErrorWithCode);

    // not a valid host
    REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/host", std::string(500, 'a')), sysrepo::ErrorWithCode);
}
