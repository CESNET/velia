/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <sysrepo-cpp/Enum.hpp>
#include "alarms.h"
#include "utils/libyang.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;

namespace {
const auto alarmInventory = "/ietf-alarms:alarms/alarm-inventory"s;
const auto alarmRpc = "/sysrepo-ietf-alarms:create-or-update-alarm";
}

namespace velia::alarms {
void push(sysrepo::Session session, const std::string& alarmId, const std::string& resource, const std::string& severity, const std::string& text)
{
    auto inputNode = session.getContext().newPath(alarmRpc, std::nullopt);

    inputNode.newPath(alarmRpc + "/resource"s, resource);
    inputNode.newPath(alarmRpc + "/alarm-type-id"s, alarmId);
    inputNode.newPath(alarmRpc + "/alarm-type-qualifier"s, "");
    inputNode.newPath(alarmRpc + "/severity"s, severity);
    inputNode.newPath(alarmRpc + "/alarm-text"s, text);

    session.sendRPC(inputNode);
}

void pushInventory(sysrepo::Session session, const std::string& alarmId, const std::string& description, const std::vector<std::string>& resources, const std::vector<std::string>& severities, WillClear willClear)
{
    const auto prefix = alarmInventory + "/alarm-type[alarm-type-id='" + alarmId + "'][alarm-type-qualifier='']";

    utils::ScopedDatastoreSwitch s(session, sysrepo::Datastore::Operational);

    session.setItem(prefix + "/will-clear", willClear == WillClear::Yes ? "true" : "false");
    session.setItem(prefix + "/description", description);

    for (const auto& severity : severities) {
        session.setItem(prefix + "/severity-level", severity);
    }

    for (const auto& resource : resources) {
        session.setItem(prefix + "/resource", resource);
    }

    session.applyChanges();
}

void addResourcesToInventory(sysrepo::Session session, const std::map<std::string, std::vector<std::string>>& resourcesPerAlarm)
{
    utils::ScopedDatastoreSwitch s(session, sysrepo::Datastore::Operational);

    for (const auto& [alarmId, resources] : resourcesPerAlarm) {
        const auto prefix = alarmInventory + "/alarm-type[alarm-type-id='" + alarmId + "'][alarm-type-qualifier='']";

        for (const auto& resource : resources) {
            session.setItem(prefix + "/resource", resource);
        }
    }
    session.applyChanges();
}
}
