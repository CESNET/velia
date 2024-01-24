/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include "trompeloeil_doctest.h"
#include <optional>
#include <sysrepo-cpp/Subscription.hpp>
#include "sysrepo-helpers/common.h"
#include "test_log_setup.h"

struct AlarmInventory {
    struct AlarmKey {
        std::string type;
        std::string qualifier;

        auto operator<=>(const AlarmKey&) const = default;
    };

    struct AllowedResourcesAndSeverities {
        std::set<std::string> resources;
        std::set<std::string> severities;
    };

    std::map<AlarmKey, AllowedResourcesAndSeverities> inventory;

    void add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::set<std::string>& resource, const std::set<std::string>& severity);
    bool contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::optional<std::string>& resource, const std::optional<std::string>& severity) const;
};

#define ALARM_INVENTORY_INSERT(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCES, SEVERITIES) LR_SIDE_EFFECT(INV.add(ALARM_TYPE, ALARM_QUALIFIER, RESOURCES, SEVERITIES))
#define ALARM_INVENTORY_CONTAINS(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE, SEVERITY) LR_WITH(INV.contains(ALARM_TYPE, ALARM_QUALIFIER, RESOURCE, SEVERITY))
