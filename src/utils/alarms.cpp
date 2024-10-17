/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <sysrepo-cpp/Enum.hpp>
#include <spdlog/spdlog.h>
#include "alarms.h"
#include "utils/benchmark.h"
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
    WITH_TIME_MEASUREMENT{};
    auto inputNode = session.getContext().newPath(alarmRpc, std::nullopt);

    inputNode.newPath(alarmRpc + "/resource"s, resource);
    inputNode.newPath(alarmRpc + "/alarm-type-id"s, alarmId);
    inputNode.newPath(alarmRpc + "/alarm-type-qualifier"s, "");
    inputNode.newPath(alarmRpc + "/severity"s, severity);
    inputNode.newPath(alarmRpc + "/alarm-text"s, text);

    spdlog::get("main")->trace("alarms::push");
    session.sendRPC(inputNode);
}

void pushInventory(sysrepo::Session session, const std::vector<AlarmInventoryEntry>& entries)
{
    WITH_TIME_MEASUREMENT{};
    utils::ScopedDatastoreSwitch s(session, sysrepo::Datastore::Operational);

    for (const auto& entry: entries) {
        const auto prefix = alarmInventory + "/alarm-type[alarm-type-id='" + entry.alarmType + "'][alarm-type-qualifier='']";

        session.setItem(prefix + "/will-clear", entry.willClear == WillClear::Yes ? "true" : "false");
        session.setItem(prefix + "/description", entry.description);

        for (const auto& severity : entry.severities) {
            session.setItem(prefix + "/severity-level", severity);
        }

        for (const auto& resource : entry.resources) {
            session.setItem(prefix + "/resource", resource);
        }
    }

    spdlog::get("main")->trace("alarms::pushInventory: {}", *session.getPendingChanges()->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
    WITH_TIME_MEASUREMENT{"pushInventory/applyChanges"};
    session.applyChanges();
}

void addResourcesToInventory(sysrepo::Session session, const std::map<std::string, std::vector<std::string>>& resourcesPerAlarm)
{
    WITH_TIME_MEASUREMENT{};
    utils::ScopedDatastoreSwitch s(session, sysrepo::Datastore::Operational);

    for (const auto& [alarmId, resources] : resourcesPerAlarm) {
        const auto prefix = alarmInventory + "/alarm-type[alarm-type-id='" + alarmId + "'][alarm-type-qualifier='']";

        for (const auto& resource : resources) {
            session.setItem(prefix + "/resource", resource);
        }
    }
    spdlog::get("main")->trace("alarms::addResourcesToInventory: {}", *session.getPendingChanges()->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
    WITH_TIME_MEASUREMENT{"addResourcesToInventory/applyChanges"};
    session.applyChanges();
}

AlarmInventoryEntry::AlarmInventoryEntry(const std::string& alarmType, const std::string& description, const std::vector<std::string>& resources, const std::vector<std::string>& severities, WillClear willClear)
    : alarmType(alarmType)
    , description(description)
    , resources(resources)
    , severities(severities)
    , willClear(willClear)
{
}
}
