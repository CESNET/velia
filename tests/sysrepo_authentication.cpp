/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "mock/system.h"
#include "pretty_printers.h"
#include "system/Authentication.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "utils/libyang.h"

using namespace std::string_literals;

TEST_CASE("Authentication")
{
    FakeAuthentication mock;
    TEST_INIT_LOGS;
    auto srConn = std::make_shared<sysrepo::Connection>();
    auto srSess = std::make_shared<sysrepo::Session>(srConn);
    velia::system::Authentication auth(srSess, velia::system::Authentication::Callbacks {
            [&mock] () { return mock.listUsers(); },
            [&mock] (const auto& user, const auto& password) { mock.changePassword(user, password); },
            [&mock] (const auto& user, const auto& key) { mock.addKey(user, key); },
            [&mock] (const auto& user, const int index) { mock.removeKey(user, index); }
    });

    auto test_srConn = std::make_shared<sysrepo::Connection>();
    auto test_srSess = std::make_shared<sysrepo::Session>(test_srConn, SR_DS_OPERATIONAL);

    using velia::system::User;
    SECTION("list users")
    {
        REQUIRE_CALL(mock, listUsers()).RETURN(std::vector{User{"root", "PASSWORDHASH", {}}});
        auto data = dataFromSysrepo(test_srSess, "/czechlight-system:authentication/users");
        decltype(data) expected = {
            {"[name='root']/name", "root"},
            {"[name='root']/password", "PASSWORDHASH"}
        };
        REQUIRE(data == expected);
    }

    SECTION("RPCs/actions")
    {
        std::string rpcPath;
        std::map<std::string, std::string> input;
        std::map<std::string, std::string> expected;
        REQUIRE_CALL(mock, listUsers()).RETURN(std::vector{User{"root", "PASSWORDHASH", {"root publickey", "root second public key"}}});
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
                expectation = NAMED_REQUIRE_CALL(mock, addKey("root", "ssh-rsa DJSANDKJANSDWA comment"));
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
                expectation = NAMED_REQUIRE_CALL(mock, addKey("root", "invalid")).THROW(std::runtime_error("Invalid key."));;
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
                expectation = NAMED_REQUIRE_CALL(mock, removeKey("root", 1));

                rpcPath = "/czechlight-system:authentication/users[name='root']/authorized-keys[index='1']/remove";

                expected = {
                    {"/result", "success"}
                };
                input = {};
            }

            SECTION("removeKey throws")
            {
                expectation = NAMED_REQUIRE_CALL(mock, removeKey("root", 0)).THROW(std::runtime_error("Can't remove last key."));;

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
