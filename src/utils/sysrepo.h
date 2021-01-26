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

void valuesToYang(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent);
void valuesPush(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session, sr_datastore_t datastore);
void valuesPush(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session);
void initLogsSysrepo();
void ensureModuleImplemented(std::shared_ptr<::sysrepo::Session> session, const std::string& module, const std::string& revision);

}
