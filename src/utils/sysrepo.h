/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <sysrepo-cpp/Session.hpp>
#include <string>

namespace velia::utils {

struct YANGPair {
    std::string m_xpath;
    std::string m_value;

    YANGPair(std::string xpath, std::string value);
};

void valuesToYang(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent);
void valuesToYang(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent);

void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session);
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session);
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session, sr_datastore_t datastore);
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session, sr_datastore_t datastore);

void initLogsSysrepo();
void ensureModuleImplemented(std::shared_ptr<::sysrepo::Session> session, const std::string& module, const std::string& revision);

}
