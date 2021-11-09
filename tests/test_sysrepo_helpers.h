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
    auto data = session.getData((xpath + "/*").c_str());
    REQUIRE(data);
    for (const auto& sibling : data->siblings()) {
        for (const auto& node : sibling.childrenDfs()) {
            if (node.schema().nodeType() == libyang::NodeType::Leaf) {
                const auto briefXPath = std::string(node.path()).substr(boost::algorithm::ends_with(xpath, ":*") ? xpath.size() - 1 : xpath.size());
                res.emplace(briefXPath, node.asTerm().valueStr());
            }

        }
    }
    return res;
}

/** @short Execute an RPC or action, return result, compacting the XPath. The rpcPath and input gets concatenated. */
auto rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, std::map<std::string, std::string> input)
{
    spdlog::get("main")->info("rpcFromSysrepo {}", rpcPath);
    auto inputNode = session.getContext().newPath(rpcPath.c_str(), nullptr);
    for (const auto& [k, v] : input) {
        inputNode.newPath((rpcPath + "/" + k).c_str(), v.c_str());
    }

    auto output = session.sendRPC(inputNode);

    std::map<std::string, std::string> res;
    for (const auto& node : output.childrenDfs()) {
        if (node.schema().nodeType() == libyang::NodeType::Leaf) {
            auto path = std::string{node.path()};
            const auto briefXPath = path.substr(rpcPath.size());
            res.emplace(briefXPath, node.asTerm().valueStr());
        }
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
