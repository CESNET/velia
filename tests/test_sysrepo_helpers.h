/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <boost/algorithm/string/predicate.hpp>
#include <map>
#include <sysrepo-cpp/Connection.hpp>
#include "test_log_setup.h"
#include "utils/sysrepo.h"

/** @short Return a subtree from sysrepo, compacting the XPath */
auto dataFromSysrepo(const sysrepo::Session& session, const std::string& xpath)
{
    spdlog::get("main")->error("dataFrom {}", xpath);
    std::map<std::string, std::string> res;
    auto data = session.getData(xpath + "/*");
    REQUIRE(data.has_value());
    for (const auto& sibling : data->findXPath(xpath)) { // Use findXPath here in case the xpath is list without keys.
        for (const auto& node : sibling.childrenDfs()) {
            const auto briefXPath = std::string(node.path()).substr(boost::algorithm::ends_with(xpath, ":*") ? xpath.size() - 1 : xpath.size());

            // We ignore the thing that's exactly the xpath we're retrieving to avoid having {"": ""} entries.
            if (briefXPath.empty()) {
                continue;
            }
            res.emplace(briefXPath, node.isTerm() ? node.asTerm().valueStr() : "");
        }
    }
    return res;
}

/** @short Execute an RPC or action, return result, compacting the XPath. The rpcPath and input gets concatenated. */
auto rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, std::map<std::string, std::string> input)
{
    spdlog::get("main")->info("rpcFromSysrepo {}", rpcPath);
    auto inputNode = session.getContext().newPath(rpcPath, std::nullopt);
    for (const auto& [k, v] : input) {
        inputNode.newPath(rpcPath + "/" + k, v);
    }

    auto output = session.sendRPC(inputNode);

    std::map<std::string, std::string> res;
    for (const auto& node : output.childrenDfs()) {
        const auto briefXPath = std::string{node.path()}.substr(rpcPath.size());

        // We ignore the thing that's exactly the xpath we're retrieving to avoid having {"": ""} entries.
        if (briefXPath.empty()) {
            continue;
        }
        res.emplace(briefXPath, node.isTerm() ? node.asTerm().valueStr() : "");
    }
    return res;
}

/** @short Return a subtree from specified sysrepo's datastore, compacting the XPath*/
auto dataFromSysrepo(sysrepo::Session session, const std::string& xpath, sysrepo::Datastore datastore)
{
    auto oldDatastore = session.activeDatastore();
    session.switchDatastore(datastore);

    auto res = dataFromSysrepo(session, xpath);

    session.switchDatastore(oldDatastore);
    return res;
}

#define TEST_SYSREPO_INIT                      \
    auto srConn = sysrepo::Connection{};       \
    auto srSess = srConn.sessionStart();

#define TEST_SYSREPO_INIT_CLIENT                     \
    auto clientConn = sysrepo::Connection{};         \
    auto client = clientConn.sessionStart();
