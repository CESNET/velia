#include "client.h"
#include <sysrepo-cpp/Session.hpp>

using namespace std::string_literals;

void invokeAlarm(sysrepo::Session session, const std::string& alarmTypeId, const std::string& alarmTypeQualifier, const std::string& resource, bool active, std::initializer_list<std::pair<std::string, std::string>> leaves) {
	auto ctx = session.getContext();
	const auto prefix = "/czechlight-alarm-manager:create-or-update-alarm"s;

	auto inp = ctx.newPath(prefix.c_str());
	inp.newPath((prefix + "/resource").c_str(), resource.c_str());
	inp.newPath((prefix + "/alarm-type-id").c_str(), alarmTypeId.c_str());
	inp.newPath((prefix + "/alarm-type-qualifier").c_str(), alarmTypeQualifier.c_str());

	inp.newPath((prefix + "/is-cleared").c_str(), active ? "false" : "true");

	static const auto VALID_LEAVES = {"alarm-text"s, "perceived-severity"s};
	/* inp.newPath((prefix + "/time-created").c_str(), "2021-01-13T17:15:50-00:00"); */
	/* inp.newPath((prefix + "/is-cleared").c_str(), "false"); */
	/* inp.newPath((prefix + "/last-raised").c_str(), "2021-01-13T17:15:50-00:00"); */
	/* inp.newPath((prefix + "/last-changed").c_str(), "2021-01-13T17:15:50-00:00"); */
	/* inp.newPath((prefix + "/perceived-severity").c_str(), "indeterminate"); */
	/* inp.newPath((prefix + "/alarm-text").c_str(), "texty"); */

	for (const auto& [key, val]: leaves) {
		if (std::find(VALID_LEAVES.begin(), VALID_LEAVES.end(), key) != VALID_LEAVES.end()) {
			inp.newPath((prefix + "/" + key).c_str(), val.c_str());
		}
	}

	session.sendRPC(inp);
}
