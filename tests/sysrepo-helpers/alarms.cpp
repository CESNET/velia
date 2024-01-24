/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "alarms.h"

void AlarmInventory::add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource)
{
    inventory[{alarmTypeId, alarmTypeQualifier}].push_back(resource);
}

bool AlarmInventory::contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource) const
{
    if (auto it = inventory.find({alarmTypeId, alarmTypeQualifier}); it != inventory.end()) {
        return std::find(it->second.begin(), it->second.end(), resource) != it->second.end();
    }
    return false;
}
