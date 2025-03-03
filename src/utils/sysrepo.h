/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <sysrepo-cpp/Session.hpp>
#include <map>
#include <string>

namespace velia::utils {

struct YANGPair {
    std::string m_xpath;
    std::string m_value;

    YANGPair(std::string xpath, std::string value);
};

using YANGData = std::vector<YANGPair>;

void valuesToYang(const YANGData& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent);
void valuesToYang(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent);

void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session);
void valuesPush(const YANGData& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session);
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session, sysrepo::Datastore datastore);
void valuesPush(const YANGData& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session, sysrepo::Datastore datastore);

void initLogsSysrepo();
void ensureModuleImplemented(::sysrepo::Session session, const std::string& module, const std::string& revision);

void setErrors(::sysrepo::Session session, const std::string& msg);

/** @brief Ensures that session switches to provided datastore and when the object gets destroyed the session switches back to the original datastore. */
class ScopedDatastoreSwitch {
    sysrepo::Session m_session;
    sysrepo::Datastore m_oldDatastore;

public:
    ScopedDatastoreSwitch(sysrepo::Session session, sysrepo::Datastore ds);
    ~ScopedDatastoreSwitch();
    ScopedDatastoreSwitch(const ScopedDatastoreSwitch&) = delete;
    ScopedDatastoreSwitch(ScopedDatastoreSwitch&&) = delete;
    ScopedDatastoreSwitch& operator=(const ScopedDatastoreSwitch&) = delete;
    ScopedDatastoreSwitch& operator=(ScopedDatastoreSwitch&&) = delete;
};
}
