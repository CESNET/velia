/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <string>
#include <sysrepo-cpp/Session.hpp>

namespace velia::health {

void createOrUpdateAlarm(sysrepo::Session session, const std::string& alarmId, const std::string& alarmQualifierType, const std::string& alarmResource, const std::string& severity, const std::string& alarmText);
void createOrUpdateAlarmInventoryEntry(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmQualifierType, const std::vector<std::string>& resource, bool willClear, const std::vector<std::string>& severities, const std::string& description);

}
