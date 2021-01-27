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

TEST_CASE("Authentication")
{
    FakeAuthentication mock;
    TEST_INIT_LOGS;
    auto srConn = std::make_shared<sysrepo::Connection>();
    auto srSess = std::make_shared<sysrepo::Session>(srConn);
    velia::system::Authentication auth(srSess, velia::system::Authentication::Callbacks {
            [&mock] (const auto& user, const auto& password) { mock.addUser(user, password); },
            [&mock] (const auto& user) { mock.removeUser(user); },
            [&mock] () { return mock.listUsers(); }
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
}
