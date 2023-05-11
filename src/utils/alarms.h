/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <optional>
#include <string>
#include <sysrepo-cpp/Session.hpp>

namespace velia::utils {
void createOrUpdateAlarm(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmQualifierType, const std::string& alarmResource, const std::string& severity, const std::string& alarmText);
void createOrUpdateAlarmInventoryEntry(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmTypeQualifier, const std::vector<std::string>& severities, bool willClear, const std::string& description);
void addResourceToAlarmInventoryEntry(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmTypeQualifier, const std::string& resource);
}
