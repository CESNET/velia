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

void valuesToYang(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent);
void valuesToYang(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent);

void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session);
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session);
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session, sysrepo::Datastore datastore);
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session, sysrepo::Datastore datastore);

void initLogsSysrepo();
void ensureModuleImplemented(::sysrepo::Session session, const std::string& module, const std::string& revision);

void setErrors(::sysrepo::Session session, const std::string& msg);
}
