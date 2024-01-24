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
#include "sysrepo-helpers/datastore.h"
#include "sysrepo-helpers/rpc.h"
#include "test_log_setup.h"

/** @brief A simple mock implementation of the alarm server */
struct AlarmWatcher {
    /** @brief Poor man's /ietf-alarms:alarms/alarm-inventory implementation in C++. */
    struct AlarmInventory {
        std::map<std::pair<std::string, std::string>, std::set<std::string>> inventory;

        void add(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource);
        bool contains(const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource) const;
    };

    AlarmInventory alarmInventory;
    DatastoreWatcher datastoreWatcher;
    RPCWatcher rpcWatcher;

    AlarmWatcher(sysrepo::Session& session);
};

// checks if the alarm is contained in AlarmInventory
#define WITH_ALARM_IN_INVENTORY(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE) LR_WITH(INV.contains(ALARM_TYPE, ALARM_QUALIFIER, RESOURCE))

// inserts the alarm in AlarmInventory as a side effect
#define INSERT_INTO_INVENTORY(INV, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE) LR_SIDE_EFFECT(INV.add(ALARM_TYPE, ALARM_QUALIFIER, RESOURCE))

#define REQUIRE_NEW_ALARM_INVENTORY_ENTRY(WATCHER, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE) \
    REQUIRE_DATASTORE_CHANGE(WATCHER.datastoreWatcher, \
                             (ValueChanges{ \
                                 {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ALARM_TYPE "'][alarm-type-qualifier='" ALARM_QUALIFIER "']/alarm-type-id", ALARM_TYPE}, \
                                 {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ALARM_TYPE "'][alarm-type-qualifier='" ALARM_QUALIFIER "']/alarm-type-qualifier", ALARM_QUALIFIER}, \
                                 {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ALARM_TYPE "'][alarm-type-qualifier='" ALARM_QUALIFIER "']/resource[1]", RESOURCE}, \
                             })) \
        .INSERT_INTO_INVENTORY(WATCHER.alarmInventory, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE)

#define REQUIRE_NEW_ALARM_INVENTORY_RESOURCE(WATCHER, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE) \
    REQUIRE_DATASTORE_CHANGE(WATCHER.datastoreWatcher, \
                             (ValueChanges{ \
                                 {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ALARM_TYPE "'][alarm-type-qualifier='" ALARM_QUALIFIER "']/resource[1]", RESOURCE}, \
                             })) \
        .INSERT_INTO_INVENTORY(WATCHER.alarmInventory, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE)

#define REQUIRE_NEW_ALARM(WATCHER, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE, SEVERITY, TEXT) \
    REQUIRE_RPC_CALL(WATCHER.rpcWatcher, (Values{ \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", TEXT}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", ALARM_TYPE}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ALARM_QUALIFIER}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", RESOURCE}, \
                                             {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", SEVERITY}, \
                                         })) \
        .WITH_ALARM_IN_INVENTORY(WATCHER.alarmInventory, ALARM_TYPE, ALARM_QUALIFIER, RESOURCE)
