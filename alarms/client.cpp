#include "client.h"
#include <sysrepo-cpp/Session.hpp>

using namespace std::string_literals;

void invokeAlarm(sysrepo::Session session, const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource, const std::string& severity, std::initializer_list<std::pair<std::string, std::string>> leaves) {
	auto ctx = session.getContext();
	const auto prefix = "/sysrepo-ietf-alarms:create-or-update-alarm"s;

	auto inp = ctx.newPath(prefix.c_str());
	inp.newPath((prefix + "/resource").c_str(), resource.c_str());
	inp.newPath((prefix + "/alarm-type-id").c_str(), alarmTypeId.c_str());
	inp.newPath((prefix + "/alarm-type-qualifier").c_str(), alarmTypeQualifier.c_str());

	inp.newPath((prefix + "/severity").c_str(), severity.c_str());

	static const auto VALID_LEAVES = {"alarm-text"s};
	for (const auto& [key, val]: leaves) {
		if (std::find(VALID_LEAVES.begin(), VALID_LEAVES.end(), key) != VALID_LEAVES.end()) {
			inp.newPath((prefix + "/" + key).c_str(), val.c_str());
		}
	}

	session.sendRPC(inp);
}
