/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <sysrepo-cpp/Enum.hpp>
#include "alarms.h"
#include "utils/UniqueResource.h"

using namespace std::string_literals;

namespace {
const auto alarmInventory = "/ietf-alarms:alarms/alarm-inventory"s;
const auto alarmRpc = "/sysrepo-ietf-alarms:create-or-update-alarm";
}

namespace velia::health {
void createOrUpdateAlarm(sysrepo::Session session, const std::string& alarmId, const std::string& alarmTypeQualifier, const std::string& resource, const std::string& severity, const std::string& text)
{
    auto inputNode = session.getContext().newPath(alarmRpc, std::nullopt);

    inputNode.newPath(alarmRpc + "/resource"s, resource);
    inputNode.newPath(alarmRpc + "/alarm-type-id"s, alarmId);
    inputNode.newPath(alarmRpc + "/alarm-type-qualifier"s, alarmTypeQualifier);
    inputNode.newPath(alarmRpc + "/severity"s, severity);
    inputNode.newPath(alarmRpc + "/alarm-text"s, text);

    session.sendRPC(inputNode);
}

void createOrUpdateAlarmInventoryEntry(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmTypeQualifier, const std::vector<std::string>& resource, bool willClear, const std::vector<std::string>& severities, const std::string& description)
{
    sysrepo::Datastore originalDs;
    auto x = make_unique_resource(
        [&]() {
            originalDs = session.activeDatastore();
            session.switchDatastore(sysrepo::Datastore::Operational);
        },
        [&]() {
            session.switchDatastore(originalDs);
        });

    const auto prefix = alarmInventory + "/alarm-type[alarm-type-id='" + alarmId + "'][alarm-type-qualifier='" + alarmTypeQualifier.value_or("") + "']";
    session.setItem(prefix + "/will-clear", willClear ? "true" : "false");
    session.setItem(prefix + "/description", description);

    for (const auto& r : resource) {
        session.setItem(prefix + "/resource", r.c_str());
    }

    for (const auto& e : severities) {
        session.setItem(prefix + "/severity-level", e.c_str());
    }

    session.applyChanges();
}

}
