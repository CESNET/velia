/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <optional>
#include <string>
#include <sysrepo-cpp/Session.hpp>

namespace velia::alarms {
enum WillClear {
    No,
    Yes,
};

void push(sysrepo::Session session, const std::string& alarmId, const std::string& alarmResource, const std::string& severity, const std::string& alarmText);
void pushInventory(sysrepo::Session session, const std::string& alarmId, const std::string& description, const std::vector<std::string>& resources, const std::vector<std::string>& severities = {}, WillClear willClear = WillClear::Yes);
void addResourcesToInventory(sysrepo::Session session, const std::string& alarmId, const std::vector<std::string>& resources);
}
