#include "trompeloeil_doctest.h"
#include <filesystem>
#include <fstream>
#include <sysrepo-cpp/utils/exception.hpp>
#include "system/JournalUpload.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/sysrepo-helpers/common.h"
#include "utils/io.h"

#define EXPECT_RESTART_UNIT expectations.emplace_back(NAMED_REQUIRE_CALL(restartMock, restartCalled()).IN_SEQUENCE(seq1));

using namespace std::literals;

struct RestartMock {
    MAKE_CONST_MOCK0(restartCalled, void());
};

TEST_CASE("Journal upload settings")
{
    trompeloeil::sequence seq1;
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

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

    std::string expectedContent;

    SECTION("Initialization")
    {
        SECTION("Presence container")
        {
            client.setItem("/czechlight-system:journal-upload/host", "upload.example.com");
            client.applyChanges();

            SECTION("Env file exists")
            {
                std::ofstream ofs(fakeEnvFile);
                ofs << "DESTINATION=192.0.2.254\n";
            }

            SECTION("No env file")
            {
            }

            expectedContent = "DESTINATION=https://upload.example.com:19532\n";
            EXPECT_RESTART_UNIT;

            syslog = std::make_unique<velia::system::JournalUpload>(srSess, fakeEnvFile, restartCb);
            waitForCompletionAndBitMore(seq1);
            REQUIRE(std::filesystem::exists(fakeEnvFile));
            REQUIRE(velia::utils::readFileToString(fakeEnvFile) == expectedContent);
        }

        SECTION("No presence container")
        {
            SECTION("Env file exists")
            {
                std::ofstream ofs(fakeEnvFile);
                ofs << "DESTINATION=192.0.2.254\n";
                EXPECT_RESTART_UNIT;
            }

            SECTION("No env file")
            {
                // no restart expected, service does not start if the file is not there
            }

            syslog = std::make_unique<velia::system::JournalUpload>(srSess, fakeEnvFile, restartCb);
            waitForCompletionAndBitMore(seq1);
            REQUIRE(!std::filesystem::exists(fakeEnvFile));
        }
    }

    SECTION("Responding to changes")
    {
        syslog = std::make_unique<velia::system::JournalUpload>(srSess, fakeEnvFile, restartCb);

        SECTION("IPv6")
        {
            SECTION("With zone")
            {
                client.setItem("/czechlight-system:journal-upload/host", "::1%lo");
                expectedContent = "DESTINATION=https://[::1%lo]:19532\n";
            }

            SECTION("Longer address")
            {
                client.setItem("/czechlight-system:journal-upload/host", "2001:0db8:0001::0ab9:C0A8:0102"); // it seems that libyang normalizes the address
                expectedContent = "DESTINATION=https://[2001:db8:1::ab9:c0a8:102]:19532\n";
            }

            EXPECT_RESTART_UNIT;
            client.applyChanges();
        }

        SECTION("Setting all leafs")
        {
            client.setItem("/czechlight-system:journal-upload/protocol", "http");
            client.setItem("/czechlight-system:journal-upload/port", "1234");
            client.setItem("/czechlight-system:journal-upload/host", "192.0.2.111");

            EXPECT_RESTART_UNIT;
            client.applyChanges();

            expectedContent = "DESTINATION=http://192.0.2.111:1234\n";
        }

        SECTION("Changing one leaf triggers restart")
        {
            client.setItem("/czechlight-system:journal-upload/host", "192.0.2.2");
            client.setItem("/czechlight-system:journal-upload/protocol", "http");
            client.setItem("/czechlight-system:journal-upload/port", "1234");

            EXPECT_RESTART_UNIT;
            client.applyChanges();

            client.setItem("/czechlight-system:journal-upload/protocol", "https");

            EXPECT_RESTART_UNIT;
            client.applyChanges();

            expectedContent = "DESTINATION=https://192.0.2.2:1234\n";
        }

        waitForCompletionAndBitMore(seq1);
        REQUIRE(std::filesystem::exists(fakeEnvFile));
        REQUIRE(velia::utils::readFileToString(fakeEnvFile) == expectedContent);
    }

    SECTION("Disabling service")
    {
        client.setItem("/czechlight-system:journal-upload/host", "127.0.0.1");
        client.applyChanges();

        EXPECT_RESTART_UNIT;
        syslog = std::make_unique<velia::system::JournalUpload>(srSess, fakeEnvFile, restartCb);

        EXPECT_RESTART_UNIT;
        client.deleteItem("/czechlight-system:journal-upload");
        client.applyChanges();

        waitForCompletionAndBitMore(seq1);
        REQUIRE(!std::filesystem::exists(fakeEnvFile));
    }

    SECTION("YANG model")
    {
        SECTION("Host leaf is mandatory")
        {
            client.setItem("/czechlight-system:journal-upload", std::nullopt);
            REQUIRE_THROWS_AS(client.applyChanges(), sysrepo::ErrorWithCode);
        }

        SECTION("Invalid protocol value")
        {
            REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/protocol", "imap"), sysrepo::ErrorWithCode);
        }

        SECTION("Invalid host")
        {
            REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/host", std::string(5000, 'a')), sysrepo::ErrorWithCode);
        }

        SECTION("WS noise")
        {
            REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/host", "ahoj.net\nVAR=val"), sysrepo::ErrorWithCode);
            REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/host", "\n"), sysrepo::ErrorWithCode);
            REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/host", " "), sysrepo::ErrorWithCode);
            REQUIRE_THROWS_AS(client.setItem("/czechlight-system:journal-upload/host", ""), sysrepo::ErrorWithCode);
        }
    }
}
