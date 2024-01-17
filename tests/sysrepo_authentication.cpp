/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/utils/exception.hpp>
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
    trompeloeil::sequence seq1;
    TEST_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    std::filesystem::path testDir = CMAKE_CURRENT_BINARY_DIR "/tests/authentication"s;
    removeDirectoryTreeIfExists(testDir);
    std::filesystem::create_directory(testDir);
    std::filesystem::create_directory(testDir / "authorized_keys");
    std::string authorized_keys_format = testDir / "authorized_keys/{USER}";
    std::string etc_passwd = testDir / "etc_passwd";
    std::string etc_shadow = testDir / "etc_shadow";
    velia::system::Authentication auth(srSess, etc_passwd, etc_shadow, authorized_keys_format, [&mock] (const auto& user, const auto& password, const auto& etc_shadow) { mock.changePassword(user, password, etc_shadow); });

    client.switchDatastore(sysrepo::Datastore::Operational);

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
        auto data = dataFromSysrepo(client, "/czechlight-system:authentication/users");
        decltype(data) expected = {
            {"[name='ci']", ""},
            {"[name='ci']/name", "ci"},
            {"[name='ci']/password-last-change", "2024-10-04T00:00:00-00:00"},
            {"[name='root']", ""},
            {"[name='root']/name", "root"},
            {"[name='root']/password-last-change", "2020-09-09T00:00:00-00:00"},
            {"[name='root']/authorized-keys[index='0']", ""},
            {"[name='root']/authorized-keys[index='0']/index", "0"},
            {"[name='root']/authorized-keys[index='0']/public-key", "ssh-rsa SOME_KEY comment"}

        };
        REQUIRE(data == expected);
    }

    SECTION("Password changes")
    {
        std::string rpcPath;
        std::map<std::string, std::string> input;
        std::map<std::string, std::string> expected;
        std::unique_ptr<trompeloeil::expectation> expectation;

        SECTION("changePassword is successful")
        {
            rpcPath = "/czechlight-system:authentication/users[name='root']/change-password";
            expectation = NAMED_REQUIRE_CALL(mock, changePassword("root", "new-password", etc_shadow));
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
            expectation = NAMED_REQUIRE_CALL(mock, changePassword("root", "new-password", etc_shadow)).THROW(std::runtime_error("Task failed succesfully."));
            expected = {
                {"/result", "failure"},
                {"/message", "Task failed succesfully."}
            };
            input = {
                {"password-cleartext", "new-password"}
            };
        }

        auto output = rpcFromSysrepo(client, rpcPath, input);
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
            SECTION("keyfile directory does not exist")
            {
                removeDirectoryTreeIfExists(testDir / "authorized_keys");
                expectedContents = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDCiBEDq8VmzBcJ23q/5GjUy8Hc18Ib20cxGEdI8McjN66eeCPc8tkeji6KT1mx15UmaJ1y+8S8cPxKi2ycdUyFpuXijDkgpuwbd3XYsOQQvMarNhyzEP7SoK5xhMy0Rcgw0Ep57JMDCEaO/V7+4lK4Mu1e+e+CyR5gVg5anGnROlRElr7h18fqCMf1JNW1tZcK5xyfUqYqnkCMKrjIFCOKqZlSo1UVJaKgWNvMx+snrBAsCUvK4N7uKniDMGt4foJBfSNQ60T1UWREbeK5B/dRnmuWJB2P43oWZB0aeEbiBpM/kGh6TE22SmTutpAk/bsgfGd6TKyOuyhkyjITbixo3F5QJ7an8LtF4Uau8CLCs14lRORBeI7a5RpZnfD/TJJ+OvpDm1LKJO3ZlILO0achrkUT1O2urM4tc6O7Fik2QjGUC9QkL4AHXIDDGjpg1or56zoR8W9Tmng6/2+8SGm4n/qxtfoifYyxqPJVUya0zwmAjkoyofoyBtrktzlH4qk= comment\n";

            }

            SECTION("keyfile directory exists")
            {
                expectedContents = "ssh-rsa SOME_KEY comment\n"
                "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDCiBEDq8VmzBcJ23q/5GjUy8Hc18Ib20cxGEdI8McjN66eeCPc8tkeji6KT1mx15UmaJ1y+8S8cPxKi2ycdUyFpuXijDkgpuwbd3XYsOQQvMarNhyzEP7SoK5xhMy0Rcgw0Ep57JMDCEaO/V7+4lK4Mu1e+e+CyR5gVg5anGnROlRElr7h18fqCMf1JNW1tZcK5xyfUqYqnkCMKrjIFCOKqZlSo1UVJaKgWNvMx+snrBAsCUvK4N7uKniDMGt4foJBfSNQ60T1UWREbeK5B/dRnmuWJB2P43oWZB0aeEbiBpM/kGh6TE22SmTutpAk/bsgfGd6TKyOuyhkyjITbixo3F5QJ7an8LtF4Uau8CLCs14lRORBeI7a5RpZnfD/TJJ+OvpDm1LKJO3ZlILO0achrkUT1O2urM4tc6O7Fik2QjGUC9QkL4AHXIDDGjpg1or56zoR8W9Tmng6/2+8SGm4n/qxtfoifYyxqPJVUya0zwmAjkoyofoyBtrktzlH4qk= comment\n";
            }

            rpcPath = "/czechlight-system:authentication/users[name='root']/add-authorized-key";

            expected = {
                {"/result", "success"}
            };
            input = {
                {"key", "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDCiBEDq8VmzBcJ23q/5GjUy8Hc18Ib20cxGEdI8McjN66eeCPc8tkeji6KT1mx15UmaJ1y+8S8cPxKi2ycdUyFpuXijDkgpuwbd3XYsOQQvMarNhyzEP7SoK5xhMy0Rcgw0Ep57JMDCEaO/V7+4lK4Mu1e+e+CyR5gVg5anGnROlRElr7h18fqCMf1JNW1tZcK5xyfUqYqnkCMKrjIFCOKqZlSo1UVJaKgWNvMx+snrBAsCUvK4N7uKniDMGt4foJBfSNQ60T1UWREbeK5B/dRnmuWJB2P43oWZB0aeEbiBpM/kGh6TE22SmTutpAk/bsgfGd6TKyOuyhkyjITbixo3F5QJ7an8LtF4Uau8CLCs14lRORBeI7a5RpZnfD/TJJ+OvpDm1LKJO3ZlILO0achrkUT1O2urM4tc6O7Fik2QjGUC9QkL4AHXIDDGjpg1or56zoR8W9Tmng6/2+8SGm4n/qxtfoifYyxqPJVUya0zwmAjkoyofoyBtrktzlH4qk= comment"}
            };


            fileToCheck = testDir / "authorized_keys/root";

            auto result = rpcFromSysrepo(client, rpcPath, input);
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

            auto result = rpcFromSysrepo(client, rpcPath, input);
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

            auto result = rpcFromSysrepo(client, rpcPath, input);
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

            auto result = rpcFromSysrepo(client, rpcPath, input);
            REQUIRE(result == expected);
        }


        REQUIRE(velia::utils::readFileToString(fileToCheck) == expectedContents);
    }

    SECTION("NACM")
    {
        std::map<std::string, std::string> input;
        const std::string prefix = "/czechlight-system:authentication/users[name='ci']";

        auto sub = srSess.initNacm();

        srSess.switchDatastore(sysrepo::Datastore::Running);
        srSess.setItem("/ietf-netconf-acm:nacm/groups/group[name='users']/user-name[.='ci']", std::nullopt);
        srSess.setItem("/ietf-netconf-acm:nacm/groups/group[name='tests']/user-name[.='test']", std::nullopt);
        srSess.applyChanges();

        FileInjector ciKeys(testDir / "authorized_keys/ci", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "ssh-rsa ci1 comment\n"
                                                                                                                                      "ssh-rsa ci2 comment\n");

        SECTION("current users auth")
        {
            std::map<std::string, std::string> result;
            client.setNacmUser("ci");

            SECTION("change password")
            {
                input = {{"password-cleartext", "blah"}};
                REQUIRE_CALL(mock, changePassword("ci", "blah", etc_shadow)).IN_SEQUENCE(seq1);
                result = rpcFromSysrepo(client, prefix + "/change-password", input);
                waitForCompletionAndBitMore(seq1);
            }

            SECTION("add key")
            {
                input = {{"key", "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAdKwJwhSfuBeve5UfVHm0cx/3Jk81Z5a/iNZadjymwl cement"}};
                result = rpcFromSysrepo(client, prefix + "/add-authorized-key", input);
            }

            SECTION("remove key")
            {
                result = rpcFromSysrepo(client, prefix + "/authorized-keys[index='0']/remove", {});
            }

            std::map<std::string, std::string> expected = {{"/result", "success"}};
            REQUIRE(result == expected);
        }

        SECTION("different user's auth")
        {
            client.setNacmUser("test");

            SECTION("change password")
            {
                input = {{"password-cleartext", "blah"}};
                REQUIRE_THROWS_WITH_AS(rpcFromSysrepo(client, prefix + "/change-password", input), "Couldn't send RPC: SR_ERR_UNAUTHORIZED\n NACM access denied by \"change-password\" node extension \"default-deny-all\". (SR_ERR_UNAUTHORIZED)\n NETCONF: protocol: access-denied: /czechlight-system:authentication/users[name='ci']/change-password: Executing the operation is denied because \"test\" NACM authorization failed.", sysrepo::ErrorWithCode);
            }

            SECTION("add key")
            {
                input = {{"key", "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAdKwJwhSfuBeve5UfVHm0cx/3Jk81Z5a/iNZadjymwl cement"}};
                REQUIRE_THROWS_WITH_AS(rpcFromSysrepo(client, prefix + "/add-authorized-key", input), "Couldn't send RPC: SR_ERR_UNAUTHORIZED\n NACM access denied by \"add-authorized-key\" node extension \"default-deny-all\". (SR_ERR_UNAUTHORIZED)\n NETCONF: protocol: access-denied: /czechlight-system:authentication/users[name='ci']/add-authorized-key: Executing the operation is denied because \"test\" NACM authorization failed.", sysrepo::ErrorWithCode);
            }

            SECTION("remove key")
            {
                REQUIRE_THROWS_WITH_AS(rpcFromSysrepo(client, prefix + "/authorized-keys[index='0']/remove", {}), "Couldn't send RPC: SR_ERR_UNAUTHORIZED\n NACM access denied by \"remove\" node extension \"default-deny-all\". (SR_ERR_UNAUTHORIZED)\n NETCONF: protocol: access-denied: /czechlight-system:authentication/users[name='ci']/authorized-keys[index='0']/remove: Executing the operation is denied because \"test\" NACM authorization failed.", sysrepo::ErrorWithCode);
            }
        }
    }
}
