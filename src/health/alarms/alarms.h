/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <optional>
#include <string>
#include <sysrepo-cpp/Session.hpp>

namespace velia::health {
void createOrUpdateAlarm(sysrepo::Session session, const std::string& alarmId, const std::optional<std::string>& alarmQualifierType, const std::string& alarmResource, const std::string& severity, const std::string& alarmText);
}
