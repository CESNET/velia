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
    auto srSess = std::make_shared<sysrepo::Session>(srConn);
    std::string_view authorized_keys_format = CMAKE_CURRENT_SOURCE_DIR "/tests/test_authorized_keys/{}";
    velia::system::Authentication auth(srSess, authorized_keys_format, velia::system::Authentication::Callbacks {
            [&mock] (std::string_view authorized_keys_format) { return mock.listUsers(authorized_keys_format); },
            [&mock] (const auto& user, const auto& password) { mock.changePassword(user, password); },
            [&mock] (std::string_view authorized_keys_format, const auto& user, const auto& key) { mock.addKey(authorized_keys_format, user, key); },
            [&mock] (std::string_view authorized_keys_format, const auto& user, const int index) { mock.removeKey(authorized_keys_format, user, index); }
    });

    auto test_srConn = std::make_shared<sysrepo::Connection>();
    auto test_srSess = std::make_shared<sysrepo::Session>(test_srConn, SR_DS_OPERATIONAL);

    using velia::system::User;
    SECTION("list users")
    {
        REQUIRE_CALL(mock, listUsers(authorized_keys_format)).RETURN(std::vector{User{"root", {}, "2019-01-01"}});
        auto data = dataFromSysrepo(test_srSess, "/czechlight-system:authentication/users");
        decltype(data) expected = {
            {"[name='root']/name", "root"},
            {"[name='root']/password-last-change", "2019-01-01"}
        };
        REQUIRE(data == expected);
    }

    SECTION("RPCs/actions")
    {
        std::string rpcPath;
        std::map<std::string, std::string> input;
        std::map<std::string, std::string> expected;
        REQUIRE_CALL(mock, listUsers(authorized_keys_format)).RETURN(std::vector{User{"root", {"root publickey", "root second public key"},  "2019-01-01"}});
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

        SECTION("add key")
        {
            SECTION("addKey is successful")
            {
                expectation = NAMED_REQUIRE_CALL(mock, addKey(authorized_keys_format, "root", "ssh-rsa DJSANDKJANSDWA comment"));
                rpcPath = "/czechlight-system:authentication/users[name='root']/add-authorized-key";

                expected = {
                    {"/result", "success"}
                };
                input = {
                    {"key", "ssh-rsa DJSANDKJANSDWA comment"}
                };
            }

            SECTION("addKey throws")
            {
                expectation = NAMED_REQUIRE_CALL(mock, addKey(authorized_keys_format, "root", "invalid")).THROW(velia::system::AuthException("Invalid key."));;
                rpcPath = "/czechlight-system:authentication/users[name='root']/add-authorized-key";

                expected = {
                    {"/result", "failure"},
                    {"/message", "Invalid key."}
                };
                input = {
                    {"key", "invalid"}
                };
            }
        }

        SECTION("remove key")
        {
            SECTION("remove is successful")
            {
                expectation = NAMED_REQUIRE_CALL(mock, removeKey(authorized_keys_format, "root", 1));

                rpcPath = "/czechlight-system:authentication/users[name='root']/authorized-keys[index='1']/remove";

                expected = {
                    {"/result", "success"}
                };
                input = {};
            }

            SECTION("removeKey throws")
            {
                expectation = NAMED_REQUIRE_CALL(mock, removeKey(authorized_keys_format, "root", 0)).THROW(velia::system::AuthException("Can't remove last key."));;

                rpcPath = "/czechlight-system:authentication/users[name='root']/authorized-keys[index='0']/remove";
                expected = {
                    {"/result", "failure"},
                    {"/message", "Can't remove last key."}
                };
                input = {};
            }
        }

        auto output = rpcFromSysrepo(test_srSess, rpcPath, input);
        REQUIRE(output == expected);
    }

}

TEST_CASE("implementations")
{
    SECTION("key manipulation")
    {
        auto keyDirectory = CMAKE_CURRENT_BINARY_DIR "/tests/test_authorized_keys/"s;
        removeDirectoryTreeIfExists(keyDirectory);
        std::filesystem::create_directory(keyDirectory);
        auto authorized_keys_format = keyDirectory + "{}";

        SECTION("listKeys")
        {
            FileInjector emptyKeyFile(keyDirectory + "empty", std::filesystem::perms::owner_read, "");
            FileInjector oneKeyFile(keyDirectory + "one-key", std::filesystem::perms::owner_read, "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\n");
            FileInjector twoKeyFile(keyDirectory + "two-key", std::filesystem::perms::owner_read,
                    "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\n"
                    "ssh-rsa KEY_PLACEHOLDER another_key\n");
            FileInjector spacesKeyFile(keyDirectory + "spaces", std::filesystem::perms::owner_read,
                    "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\n\n"
                    "    ssh-rsa KEY_PLACEHOLDER another_key\n"
                    "    \n");
            std::vector<std::string> expected;
            std::vector<std::string> result;

            SECTION("one-key")
            {
                expected = {"ssh-rsa KEY_PLACEHOLDER ahoj@hostname"};
                result = velia::system::impl::listKeys(authorized_keys_format, "one-key");
            }

            SECTION("two-key")
            {
                expected = {"ssh-rsa KEY_PLACEHOLDER ahoj@hostname", "ssh-rsa KEY_PLACEHOLDER another_key"};
                result = velia::system::impl::listKeys(authorized_keys_format, "two-key");
            }

            SECTION("spaces")
            {
                expected = {"ssh-rsa KEY_PLACEHOLDER ahoj@hostname", "    ssh-rsa KEY_PLACEHOLDER another_key"};
                result = velia::system::impl::listKeys(authorized_keys_format, "spaces");
            }

            SECTION("empty")
            {
                result = velia::system::impl::listKeys(authorized_keys_format, "empty");
            }

            SECTION("key file doesn't exist")
            {
                result = velia::system::impl::listKeys(authorized_keys_format, "no-key-file");
            }

            REQUIRE(result == expected);
        }

        SECTION("add remove")
        {
            std::filesystem::path fileToCheck;
            std::string expectedContents;
            FileInjector oneKeyFile(keyDirectory + "one-key", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\n");
            FileInjector twoKeyFile(keyDirectory + "two-key", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                    "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\n"
                    "ssh-rsa KEY_PLACEHOLDER another_key\n");

            SECTION("addKey")
            {
                SECTION("valid key")
                {
                    velia::system::impl::addKey(authorized_keys_format, "one-key", "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDCiBEDq8VmzBcJ23q/5GjUy8Hc18Ib20cxGEdI8McjN66eeCPc8tkeji6KT1mx15UmaJ1y+8S8cPxKi2ycdUyFpuXijDkgpuwbd3XYsOQQvMarNhyzEP7SoK5xhMy0Rcgw0Ep57JMDCEaO/V7+4lK4Mu1e+e+CyR5gVg5anGnROlRElr7h18fqCMf1JNW1tZcK5xyfUqYqnkCMKrjIFCOKqZlSo1UVJaKgWNvMx+snrBAsCUvK4N7uKniDMGt4foJBfSNQ60T1UWREbeK5B/dRnmuWJB2P43oWZB0aeEbiBpM/kGh6TE22SmTutpAk/bsgfGd6TKyOuyhkyjITbixo3F5QJ7an8LtF4Uau8CLCs14lRORBeI7a5RpZnfD/TJJ+OvpDm1LKJO3ZlILO0achrkUT1O2urM4tc6O7Fik2QjGUC9QkL4AHXIDDGjpg1or56zoR8W9Tmng6/2+8SGm4n/qxtfoifYyxqPJVUya0zwmAjkoyofoyBtrktzlH4qk= test-key");
                    expectedContents = "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\nssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDCiBEDq8VmzBcJ23q/5GjUy8Hc18Ib20cxGEdI8McjN66eeCPc8tkeji6KT1mx15UmaJ1y+8S8cPxKi2ycdUyFpuXijDkgpuwbd3XYsOQQvMarNhyzEP7SoK5xhMy0Rcgw0Ep57JMDCEaO/V7+4lK4Mu1e+e+CyR5gVg5anGnROlRElr7h18fqCMf1JNW1tZcK5xyfUqYqnkCMKrjIFCOKqZlSo1UVJaKgWNvMx+snrBAsCUvK4N7uKniDMGt4foJBfSNQ60T1UWREbeK5B/dRnmuWJB2P43oWZB0aeEbiBpM/kGh6TE22SmTutpAk/bsgfGd6TKyOuyhkyjITbixo3F5QJ7an8LtF4Uau8CLCs14lRORBeI7a5RpZnfD/TJJ+OvpDm1LKJO3ZlILO0achrkUT1O2urM4tc6O7Fik2QjGUC9QkL4AHXIDDGjpg1or56zoR8W9Tmng6/2+8SGm4n/qxtfoifYyxqPJVUya0zwmAjkoyofoyBtrktzlH4qk= test-key\n";
                    fileToCheck = keyDirectory + "one-key";
                }

                SECTION("invalid key")
                {
                    expectedContents = "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\n";
                    REQUIRE_THROWS(velia::system::impl::addKey(authorized_keys_format, "one-key", "invalid key"));
                    fileToCheck = keyDirectory + "one-key";
                }
            }

            SECTION("removeKey")
            {
                SECTION("valid removal")
                {
                    velia::system::impl::removeKey(authorized_keys_format, "two-key", 0);
                    expectedContents = "ssh-rsa KEY_PLACEHOLDER another_key\n";
                    fileToCheck = keyDirectory + "two-key";
                }

                SECTION("can't remove last key")
                {
                    REQUIRE_THROWS(velia::system::impl::removeKey(authorized_keys_format, "one-key", 0));
                    expectedContents = "ssh-rsa KEY_PLACEHOLDER ahoj@hostname\n";
                    fileToCheck = keyDirectory + "one-key";
                }
            }

            REQUIRE(velia::utils::readFileToString(fileToCheck) == expectedContents);
        }

    }

    SECTION("listUsers")
    {
        // This is difficult to test, so let's check we get at least some results
        auto users = velia::system::impl::listUsers("");
        REQUIRE(users.size() > 2);
        REQUIRE(std::any_of(users.begin(), users.end(), [] (const auto& user) { return user.name == "root"; }));
        REQUIRE(std::any_of(users.begin(), users.end(), [] (const auto& user) { return user.name != "root"; }));
    }
}
