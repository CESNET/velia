/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "fs-helpers/FileInjector.h"
#include "fs-helpers/utils.h"
#include "mock/system.h"
#include "pretty_printers.h"
#include "system/Authentication.h"
#include "system_vars.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"
#include "utils/io.h"
#include "utils/libyang.h"

using namespace std::string_literals;

TEST_CASE("Authentication")
{
    FakeAuthentication mock;
    TEST_INIT_LOGS;
    auto srConn = std::make_shared<sysrepo::Connection>();
    std::filesystem::path testDir = CMAKE_CURRENT_BINARY_DIR "/tests/authentication"s;
    removeDirectoryTreeIfExists(testDir);
    std::filesystem::create_directory(testDir);
    std::filesystem::create_directory(testDir / "authorized_keys");
    auto srSess = std::make_shared<sysrepo::Session>(srConn);
    std::string authorized_keys_format = testDir / "authorized_keys/{USER}";
    std::string etc_passwd = testDir / "etc_passwd";
    std::string etc_shadow = testDir / "etc_shadow";
    velia::system::Authentication auth(srSess, etc_passwd, etc_shadow, authorized_keys_format, [&mock] (const auto& user, const auto& password) { mock.changePassword(user, password); });

    auto test_srConn = std::make_shared<sysrepo::Connection>();
    auto test_srSess = std::make_shared<sysrepo::Session>(test_srConn, SR_DS_OPERATIONAL);

    FileInjector passwd(etc_passwd, std::filesystem::perms::owner_read,
        "root:x:0:0::/root:/bin/bash\n"
        "ci:x:1000:1000::/home/ci:/bin/bash\n"

    );
    FileInjector shadow(etc_shadow, std::filesystem::perms::owner_read,
        "root::18514::::::\n"
        "ci::20000::::::\n"
    );

    using velia::system::User;
    SECTION("list users")
    {
        FileInjector rootKeys(testDir / "authorized_keys/root", std::filesystem::perms::owner_read,
            "ssh-rsa SOME_KEY comment"
        );
        auto data = dataFromSysrepo(test_srSess, "/czechlight-system:authentication/users");
        decltype(data) expected = {
            {"[name='ci']/name", "ci"},
            {"[name='ci']/password-last-change", "2024-10-04T00:00:00Z"},
            {"[name='root']/name", "root"},
            {"[name='root']/password-last-change", "2020-09-09T00:00:00Z"},
            {"[name='root']/authorized-keys[index='0']", ""},
            {"[name='root']/authorized-keys[index='0']/index", "0"},
            {"[name='root']/authorized-keys[index='0']/public-key", "ssh-rsa SOME_KEY comment"}

        };
        REQUIRE(data == expected);
    }

    SECTION("RPCs/actions")
    {
        std::string rpcPath;
        std::map<std::string, std::string> input;
        std::map<std::string, std::string> expected;
        std::unique_ptr<trompeloeil::expectation> expectation;

        SECTION("change password")
        {
            SECTION("changePassword is successful")
            {
                rpcPath = "/czechlight-system:authentication/users[name='root']/change-password";
                expectation = NAMED_REQUIRE_CALL(mock, changePassword("root", "new-password"));
                expected = {
                    {"/result", "success"}
                };
                input = {
                    {"password-cleartext", "new-password"}
                };
            }

            SECTION("changePassword throws")
            {
                rpcPath = "/czechlight-system:authentication/users[name='root']/change-password";
                expectation = NAMED_REQUIRE_CALL(mock, changePassword("root", "new-password")).THROW(std::runtime_error("Task failed succesfully."));
                expected = {
                    {"/result", "failure"},
                    {"/message", "Task failed succesfully."}
                };
                input = {
                    {"password-cleartext", "new-password"}
                };
            }
        }

        auto output = rpcFromSysrepo(test_srSess, rpcPath, input);
        REQUIRE(output == expected);
    }

    SECTION("keys")
    {
        std::string rpcPath;
        std::map<std::string, std::string> input;
        std::map<std::string, std::string> expected;

        std::filesystem::path fileToCheck;
        std::string expectedContents;

        FileInjector rootKeys(testDir / "authorized_keys/root", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            "ssh-rsa SOME_KEY comment\n"
        );

        FileInjector ciKeys(testDir / "authorized_keys/ci", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            "ssh-rsa ci1 comment\n"
            "ssh-rsa ci2 comment\n"
        );

        SECTION("add a key")
        {
            rpcPath = "/czechlight-system:authentication/users[name='root']/add-authorized-key";

            expected = {
                {"/result", "success"}
            };
            input = {
                {"key", "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDCiBEDq8VmzBcJ23q/5GjUy8Hc18Ib20cxGEdI8McjN66eeCPc8tkeji6KT1mx15UmaJ1y+8S8cPxKi2ycdUyFpuXijDkgpuwbd3XYsOQQvMarNhyzEP7SoK5xhMy0Rcgw0Ep57JMDCEaO/V7+4lK4Mu1e+e+CyR5gVg5anGnROlRElr7h18fqCMf1JNW1tZcK5xyfUqYqnkCMKrjIFCOKqZlSo1UVJaKgWNvMx+snrBAsCUvK4N7uKniDMGt4foJBfSNQ60T1UWREbeK5B/dRnmuWJB2P43oWZB0aeEbiBpM/kGh6TE22SmTutpAk/bsgfGd6TKyOuyhkyjITbixo3F5QJ7an8LtF4Uau8CLCs14lRORBeI7a5RpZnfD/TJJ+OvpDm1LKJO3ZlILO0achrkUT1O2urM4tc6O7Fik2QjGUC9QkL4AHXIDDGjpg1or56zoR8W9Tmng6/2+8SGm4n/qxtfoifYyxqPJVUya0zwmAjkoyofoyBtrktzlH4qk= comment"}
            };

            expectedContents = "ssh-rsa SOME_KEY comment\n"
                "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDCiBEDq8VmzBcJ23q/5GjUy8Hc18Ib20cxGEdI8McjN66eeCPc8tkeji6KT1mx15UmaJ1y+8S8cPxKi2ycdUyFpuXijDkgpuwbd3XYsOQQvMarNhyzEP7SoK5xhMy0Rcgw0Ep57JMDCEaO/V7+4lK4Mu1e+e+CyR5gVg5anGnROlRElr7h18fqCMf1JNW1tZcK5xyfUqYqnkCMKrjIFCOKqZlSo1UVJaKgWNvMx+snrBAsCUvK4N7uKniDMGt4foJBfSNQ60T1UWREbeK5B/dRnmuWJB2P43oWZB0aeEbiBpM/kGh6TE22SmTutpAk/bsgfGd6TKyOuyhkyjITbixo3F5QJ7an8LtF4Uau8CLCs14lRORBeI7a5RpZnfD/TJJ+OvpDm1LKJO3ZlILO0achrkUT1O2urM4tc6O7Fik2QjGUC9QkL4AHXIDDGjpg1or56zoR8W9Tmng6/2+8SGm4n/qxtfoifYyxqPJVUya0zwmAjkoyofoyBtrktzlH4qk= comment\n";

            fileToCheck = testDir / "authorized_keys/root";

            auto result = rpcFromSysrepo(test_srSess, rpcPath, input);
            REQUIRE(result == expected);
        }

        SECTION("adding invalid key")
        {
            rpcPath = "/czechlight-system:authentication/users[name='root']/add-authorized-key";

            expected = {
                {"/result", "failure"},
                {"/message", "Key is not a valid SSH public key: " SSH_KEYGEN_EXECUTABLE " returned non-zero exit code 255\nssh-rsa INVALID comment"}
            };
            input = {
                {"key", "ssh-rsa INVALID comment"}
            };

            expectedContents = "ssh-rsa SOME_KEY comment\n";

            fileToCheck = testDir / "authorized_keys/root";

            auto result = rpcFromSysrepo(test_srSess, rpcPath, input);
            REQUIRE(result == expected);
        }

        SECTION("remove key")
        {
            rpcPath = "/czechlight-system:authentication/users[name='ci']/authorized-keys[index='0']/remove";

            expected = {
                {"/result", "success"},
            };

            expectedContents = "ssh-rsa ci2 comment\n";

            fileToCheck = testDir / "authorized_keys/ci";

            auto result = rpcFromSysrepo(test_srSess, rpcPath, input);
            REQUIRE(result == expected);

        }

        SECTION("remove last key")
        {
            rpcPath = "/czechlight-system:authentication/users[name='root']/authorized-keys[index='0']/remove";

            expected = {
                {"/result", "failure"},
                {"/message", "Can't remove last key."},
            };

            expectedContents = "ssh-rsa SOME_KEY comment\n";

            fileToCheck = testDir / "authorized_keys/root";

            auto result = rpcFromSysrepo(test_srSess, rpcPath, input);
            REQUIRE(result == expected);
        }


        REQUIRE(velia::utils::readFileToString(fileToCheck) == expectedContents);
    }

}
