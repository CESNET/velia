/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Subscription.hpp>
#include "sysrepo-helpers/common.h"
#include "test_log_setup.h"

struct AlarmInventory {
    std::map<std::pair<std::string, std::string>, std::set<std::string>> inventory;

    void add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::set<std::string>& resources);
    bool contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource) const;
};


#define ALARM_INVENTORY_INSERT(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE) LR_SIDE_EFFECT(INV.add(ALARM_TYPE, ALARM_QUALIFIER, RESOURCE))
#define ALARM_INVENTORY_CONTAINS(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE) LR_WITH(INV.contains(ALARM_TYPE, ALARM_QUALIFIER, RESOURCE))
