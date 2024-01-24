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

inline ValueChanges constructAlarmInventoryChange(const std::string& alarmType, const std::string& alarmQualifier, const std::set<std::string>& resources, const std::set<std::string>& severities)
{
    const std::string prefix = "/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" + alarmType + "'][alarm-type-qualifier='" + alarmQualifier + "']";

    ValueChanges ret{
        {prefix + "/alarm-type-id", alarmType},
        {prefix + "/alarm-type-qualifier", alarmQualifier},
    };

    size_t i = 1; /* YANG uses 1-based indexing */
    for (const auto& severity : severities) {
        ret.emplace(prefix + "/severity-level[" + std::to_string(i++) + "]", severity);
    }

    i = 1;
    for (const auto& resource : resources) {
        ret.emplace(prefix + "/resource[" + std::to_string(i++) + "]", resource);
    }

    return ret;
}

/** @brief A simple mock implementation of the alarm server */
struct AlarmWatcher {
    /** @brief Poor man's /ietf-alarms:alarms/alarm-inventory implementation in C++. */
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

        void add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::set<std::string>& resources, const std::set<std::string>& severities);
        bool contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::optional<std::string>& resource, const std::optional<std::string>& severity) const;
    };

    AlarmInventory alarmInventory;
    DatastoreWatcher datastoreWatcher;
    RPCWatcher rpcWatcher;

    AlarmWatcher(sysrepo::Session& session);
};

// checks if the alarm is contained in AlarmInventory
#define REQUIRE_ALARM_IN_ALARM_INVENTORY(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE, SEVERITY) LR_WITH(INV.contains(ALARM_TYPE, ALARM_QUALIFIER, RESOURCE, SEVERITY))

// inserts the alarm in AlarmInventory as a side effect
#define INSERT_INTO_ALARM_INVENTORY(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCES, SEVERITIES) LR_SIDE_EFFECT(INV.add(ALARM_TYPE, ALARM_QUALIFIER, RESOURCES, SEVERITIES))

#define REQUIRE_NEW_ALARM_INVENTORY_ENTRY(WATCHER, ALARM_TYPE, ALARM_QUALIFIER, RESOURCES, SEVERITIES)                                    \
    REQUIRE_DATASTORE_CHANGE(WATCHER.datastoreWatcher, constructAlarmInventoryChange(ALARM_TYPE, ALARM_QUALIFIER, RESOURCES, SEVERITIES)) \
        .INSERT_INTO_ALARM_INVENTORY(WATCHER.alarmInventory, ALARM_TYPE, ALARM_QUALIFIER, RESOURCES, SEVERITIES)

#define REQUIRE_NEW_ALARM_INVENTORY_RESOURCE(WATCHER, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE)                                                                                           \
    REQUIRE_DATASTORE_CHANGE(WATCHER.datastoreWatcher,                                                                                                                                 \
                             (ValueChanges{                                                                                                                                            \
                                 {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ALARM_TYPE "'][alarm-type-qualifier='" ALARM_QUALIFIER "']/resource[1]", RESOURCE}, \
                             }))                                                                                                                                                       \
        .INSERT_INTO_ALARM_INVENTORY(WATCHER.alarmInventory, ALARM_TYPE, ALARM_QUALIFIER, (std::set<std::string>{RESOURCE}), (std::set<std::string>{}))

#define REQUIRE_NEW_ALARM(WATCHER, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE, SEVERITY, TEXT)                                           \
    REQUIRE_RPC_CALL(WATCHER.rpcWatcher, (Values{                                                                                   \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},                      \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", TEXT},                      \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", ALARM_TYPE},             \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ALARM_QUALIFIER}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", RESOURCE},                    \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", SEVERITY},                    \
                                         }))                                                                                        \
        .REQUIRE_ALARM_IN_ALARM_INVENTORY(WATCHER.alarmInventory, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE, SEVERITY)
