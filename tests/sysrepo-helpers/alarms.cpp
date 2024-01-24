/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "alarms.h"

void AlarmInventory::add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::set<std::string>& resources, const std::set<std::string>& severities)
{
    auto& alarm = inventory[{alarmTypeId, alarmTypeQualifier}];
    alarm.resources.insert(resources.begin(), resources.end());
    alarm.severities.insert(severities.begin(), severities.end());
}

bool AlarmInventory::contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::optional<std::string>& resource, const std::optional<std::string>& severity) const
{
    if (auto it = inventory.find({alarmTypeId, alarmTypeQualifier}); it != inventory.end()) {
        const auto& alarm = it->second;

        if (resource && !alarm.resources.empty() && !alarm.resources.contains(*resource)) {
            return false;
        }

        if (severity && *severity != "cleared" && !alarm.severities.empty() && !alarm.severities.contains(*severity)) {
            return false;
        }
    }
    return true;
}
