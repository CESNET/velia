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
#include "sysrepo-helpers/datastore.h"
#include "sysrepo-helpers/rpc.h"
#include "test_log_setup.h"
#include "utils/alarms.h"

inline ValueChanges constructAlarmInventoryChange(const std::vector<velia::alarms::AlarmInventoryEntry>& entries)
{
    ValueChanges ret;

    for (const auto& entry : entries) {
        const std::string prefix = "/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" + entry.alarmType + "'][alarm-type-qualifier='']";

        ret.emplace(prefix + "/alarm-type-id", entry.alarmType);
        ret.emplace(prefix + "/alarm-type-qualifier", "");

        ret.emplace(prefix + "/will-clear", entry.willClear ? "true" : "false");
        ret.emplace(prefix + "/description", entry.description);

        size_t i = 1; /* YANG uses 1-based indexing */
        for (const auto& severity : entry.severities) {
            ret.emplace(prefix + "/severity-level[" + std::to_string(i++) + "]", severity);
        }

        i = 1;
        for (const auto& resource : entry.resources) {
            ret.emplace(prefix + "/resource[" + std::to_string(i++) + "]", resource);
        }
    }

    return ret;
}

inline ValueChanges constructAlarmInventoryResourceChange(const std::vector<std::string>& alarmTypeIds, const std::vector<std::string>& resources) {
    ValueChanges ret;

    for (const auto& alarmType : alarmTypeIds) {
        const std::string prefix = "/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" + alarmType + "'][alarm-type-qualifier='']";

        size_t i = 1;
        for (const auto& resource : resources) {
            ret.emplace(prefix + "/resource[" + std::to_string(i++) + "]", resource);
        }
    }

    return ret;
}

/** @brief A simple mock implementation of the alarm server */
struct AlarmWatcher {
    /** @brief Poor man's /ietf-alarms:alarms/alarm-inventory implementation in C++. */
    struct AlarmInventory {
        using AlarmType = std::string;

        struct AllowedResourcesAndSeverities {
            std::set<std::string> resources;
            std::set<std::string> severities;
        };

        std::map<AlarmType, AllowedResourcesAndSeverities> inventory;

        void add(const std::vector<std::string>& alarmTypeIds, const std::vector<std::string>& resources, const std::vector<std::string>& severities);
        void add(const std::vector<velia::alarms::AlarmInventoryEntry>& entries);
        bool contains(const std::string& alarmTypeId, const std::optional<std::string>& resource, const std::optional<std::string>& severity) const;
    };

    AlarmInventory alarmInventory;
    DatastoreWatcher datastoreWatcher;
    RPCWatcher rpcWatcher;

    AlarmWatcher(sysrepo::Session& session);
};

// checks if the alarm is contained in AlarmInventory
#define WITH_ALARM_IN_INVENTORY(INV, ALARM_TYPE, RESOURCE, SEVERITY) \
    LR_WITH(INV.contains(ALARM_TYPE, RESOURCE, SEVERITY))

// inserts the alarm in AlarmInventory as a side effect
#define INSERT_INTO_INVENTORY(INV, ALARM_TYPES, RESOURCES, SEVERITIES) \
    LR_SIDE_EFFECT(INV.add(ALARM_TYPES, RESOURCES, SEVERITIES))
#define INSERT_INTO_INVENTORY_MANY(INV, ALARMS) \
    LR_SIDE_EFFECT(INV.add(ALARMS))

#define REQUIRE_NEW_ALARM_INVENTORY_ENTRIES(WATCHER, ALARMS) \
    REQUIRE_DATASTORE_CHANGE(WATCHER.datastoreWatcher, constructAlarmInventoryChange(ALARMS)) \
        .INSERT_INTO_INVENTORY_MANY(WATCHER.alarmInventory, ALARMS)

#define REQUIRE_NEW_ALARM_INVENTORY_RESOURCES(WATCHER, ALARM_TYPES, RESOURCES) \
    REQUIRE_DATASTORE_CHANGE(WATCHER.datastoreWatcher, constructAlarmInventoryResourceChange(ALARM_TYPES, RESOURCES)) \
        .INSERT_INTO_INVENTORY(WATCHER.alarmInventory, ALARM_TYPES, RESOURCES, (std::vector<std::string>{}))

#define REQUIRE_NEW_ALARM(WATCHER, ALARM_TYPE, RESOURCE, SEVERITY, TEXT) \
    REQUIRE_RPC_CALL(WATCHER.rpcWatcher, (Values{ \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", TEXT}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", ALARM_TYPE}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", RESOURCE}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", SEVERITY}, \
                                         })) \
        .WITH_ALARM_IN_INVENTORY(WATCHER.alarmInventory, ALARM_TYPE, RESOURCE, SEVERITY)
