#pragma once
#include <initializer_list>
#include <sysrepo-cpp/Session.hpp>

void invokeAlarm(sysrepo::Session session, const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource, bool active, std::initializer_list<std::pair<std::string, std::string>> leaves = {});
