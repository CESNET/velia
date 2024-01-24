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

void AlarmWatcher::AlarmInventory::add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource)
{
    inventory[{alarmTypeId, alarmTypeQualifier}].insert(resource);
}

bool AlarmWatcher::AlarmInventory::contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource) const
{
    if (auto it = inventory.find({alarmTypeId, alarmTypeQualifier}); it != inventory.end()) {
        return it->second.contains(resource);
    }
    return false;
}
