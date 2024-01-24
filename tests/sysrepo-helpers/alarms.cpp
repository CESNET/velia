/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "alarms.h"

void AlarmInventory::add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::set<std::string>& resources)
{
    inventory[{alarmTypeId, alarmTypeQualifier}].insert(resources.begin(), resources.end());
}

bool AlarmInventory::contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource) const
{
    if (auto it = inventory.find({alarmTypeId, alarmTypeQualifier}); it != inventory.end()) {
        return it->second.contains(resource);
    }
    return false;
}
