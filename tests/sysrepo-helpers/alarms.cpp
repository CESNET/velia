/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "alarms.h"

AlarmWatcher::AlarmWatcher(sysrepo::Session& session)
    : datastoreWatcher(session, "/ietf-alarms:alarms/alarm-inventory")
    , rpcWatcher(session, "/sysrepo-ietf-alarms:create-or-update-alarm")
{
}

void AlarmWatcher::AlarmInventory::add(const std::set<std::string>& alarmTypeIds, const std::set<std::string>& resources, const std::set<std::string>& severities)
{
    for (const auto& type : alarmTypeIds) {
        auto& alarm = inventory[type];
        alarm.resources.insert(resources.begin(), resources.end());
        alarm.severities.insert(severities.begin(), severities.end());
    }
}

bool AlarmWatcher::AlarmInventory::contains(const std::string& alarmTypeId, const std::optional<std::string>& resource, const std::optional<std::string>& severity) const
{
    if (auto it = inventory.find(alarmTypeId); it != inventory.end()) {
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
