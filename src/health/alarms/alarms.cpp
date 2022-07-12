/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include "alarms.h"

using namespace std::string_literals;

namespace {
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
}
