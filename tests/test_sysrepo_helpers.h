/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <map>
#include <sysrepo-cpp/Session.hpp>
#include "sysrepo/Logging.h"
#include "test_log_setup.h"
#include "utils/string.h"

/** @short Return a subtree from sysrepo, compacting the XPath */
auto dataFromSysrepo(const std::shared_ptr<sysrepo::Session>& session, const std::string& xpath)
{
    spdlog::get("main")->error("dataFrom {}", xpath);
    std::map<std::string, std::string> res;
    auto vals = session->get_items((xpath + "//*").c_str());
    REQUIRE(!!vals);
    for (size_t i = 0; i < vals->val_cnt(); ++i) {
        const auto& v = vals->val(i);
        const auto briefXPath = std::string(v->xpath()).substr(velia::utils::endsWith(xpath, ":*") ? xpath.size() - 1 : xpath.size());
        res.emplace(briefXPath, v->val_to_string());
    }
    return res;
}

#define TEST_SYSREPO_INIT                                     \
    auto srConn = std::make_shared<sysrepo::Connection>();    \
    auto srSess = std::make_shared<sysrepo::Session>(srConn); \
    auto srSubs = std::make_shared<sysrepo::Subscribe>(srSess);

#define TEST_SYSREPO_INIT_LOGS  \
    IMPL_TEST_INIT_LOGS_1       \
    velia::sysrepo::initLogs(); \
    IMPL_TEST_INIT_LOGS_2
