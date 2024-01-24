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
#include "common.h"
#include "test_log_setup.h"
#include "utils/UniqueResource.h"
#include "utils/sysrepo.h"

/** @short Return a subtree from sysrepo, compacting the XPath */
Values dataFromSysrepo(const sysrepo::Session& session, const std::string& xpath)
{
    spdlog::get("main")->error("dataFrom {}", xpath);
    Values res;
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
Values rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, Values input)
{
    spdlog::get("main")->info("rpcFromSysrepo {}", rpcPath);
    auto inputNode = session.getContext().newPath(rpcPath, std::nullopt);
    for (const auto& [k, v] : input) {
        inputNode.newPath(rpcPath + "/" + k, v);
    }

    auto output = session.sendRPC(inputNode);

    Values res;
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
Values dataFromSysrepo(sysrepo::Session session, const std::string& xpath, sysrepo::Datastore datastore)
{
    sysrepo::Datastore originalDS;
    auto restoreDatastore = velia::utils::make_unique_resource(
        [&]() {
            originalDS = session.activeDatastore();
            session.switchDatastore(datastore);
        },
        [&]() {
            session.switchDatastore(originalDS);
        });

    auto res = dataFromSysrepo(session, xpath);
    return res;
}

std::string moduleFromXpath(const std::string& xpath)
{
    auto pos = xpath.find(":");
    if (pos == 0 || pos == std::string::npos || xpath[0] != '/') {
        throw std::logic_error{"module_from_xpath: malformed XPath " + xpath};
    }
    return xpath.substr(1, pos - 1);
}
