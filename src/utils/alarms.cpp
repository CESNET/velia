/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <sysrepo-cpp/Enum.hpp>
#include "alarms.h"
#include "utils/UniqueResource.h"
#include "utils/libyang.h"

using namespace std::string_literals;

namespace {
const auto alarmInventory = "/ietf-alarms:alarms/alarm-inventory"s;
const auto alarmRpc = "/sysrepo-ietf-alarms:create-or-update-alarm";
}

namespace velia::utils {
void createOrUpdateAlarm(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmTypeQualifier, const std::string& resource, const std::string& severity, const std::string& text)
{
    auto inputNode = session.getContext().newPath(alarmRpc, std::nullopt);

    inputNode.newPath(alarmRpc + "/resource"s, resource);
    inputNode.newPath(alarmRpc + "/alarm-type-id"s, alarmId);
    inputNode.newPath(alarmRpc + "/alarm-type-qualifier"s, alarmTypeQualifier.value_or(""));
    inputNode.newPath(alarmRpc + "/severity"s, severity);
    inputNode.newPath(alarmRpc + "/alarm-text"s, text);

    session.sendRPC(inputNode);
}

void createOrUpdateAlarmInventoryEntry(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmTypeQualifier, const std::vector<std::string>& severities, bool willClear, const std::string& description)
{
    const auto prefix = alarmInventory + "/alarm-type[alarm-type-id='" + alarmId + "'][alarm-type-qualifier='" + alarmTypeQualifier.value_or("") + "']";

    sysrepo::Datastore originalDS;
    auto restoreDatastore = utils::make_unique_resource(
        [&]() {
            originalDS = session.activeDatastore();
            session.switchDatastore(sysrepo::Datastore::Operational);
        },
        [&]() {
            session.switchDatastore(originalDS);
        });

    session.setItem(prefix + "/will-clear", willClear ? "true" : "false");
    session.setItem(prefix + "/description", description);

    for (const auto& severity : severities) {
        session.setItem(prefix + "/severity-level", severity);
    }

    session.applyChanges();
}

void addResourceToAlarmInventoryEntry(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmTypeQualifier, const std::string& resource)
{
    const auto prefix = alarmInventory + "/alarm-type[alarm-type-id='" + alarmId + "'][alarm-type-qualifier='" + alarmTypeQualifier.value_or("") + "']";

    sysrepo::Datastore originalDS;
    auto restoreDatastore = utils::make_unique_resource(
        [&]() {
            originalDS = session.activeDatastore();
            session.switchDatastore(sysrepo::Datastore::Operational);
        },
        [&]() {
            session.switchDatastore(originalDS);
        });

    session.setItem(prefix + "/resource", resource);
    session.applyChanges();
}
}
