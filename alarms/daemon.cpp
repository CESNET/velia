#include <iostream>
#include <libyang-cpp/DataNode.hpp>
#include <string>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include "daemon.h"

using namespace std::string_literals;

std::tuple<std::string, std::string, std::string> getKeys(const libyang::DataNode& node) {
	auto type =     std::string(node.findPath("alarm-type-id").value().asTerm().valueStr());
	auto qual =     std::string(node.findPath("alarm-type-qualifier").value().asTerm().valueStr());
	auto resource = std::string(node.findPath("resource").value().asTerm().valueStr());
	return std::make_tuple(type, qual, resource);
}
bool getActive(const libyang::DataNode& node) {
	auto cleared = std::string(node.findPath("is-cleared").value().asTerm().valueStr());
	return cleared == "false";
}

struct ShelvedAlarm {
	std::string name;
	std::vector<std::pair<std::string, std::string>> alarm_types;
};

std::vector<ShelvedAlarm> fetchControlShelf(auto session) {
	auto oldDs = session.activeDatastore();
	session.switchDatastore(sysrepo::Datastore::Running);

	std::vector<ShelvedAlarm> res;

	if (auto data = session.getData("/ietf-alarms:alarms")) {
		auto s = data.value().findXPath("/ietf-alarms:alarms/control/alarm-shelving/shelf");

		ShelvedAlarm shelvedAlarm;
		for (const auto& shelfNode : s) {
			shelvedAlarm.name = shelfNode.findPath("name").value().asTerm().valueStr();

			for (const auto& alarmTypeNode : shelfNode.findXPath("alarm-type")) {
				shelvedAlarm.alarm_types.emplace_back(
						alarmTypeNode.findPath("alarm-type-id").value().asTerm().valueStr(),
						alarmTypeNode.findPath("alarm-type-qualifier-match").value().asTerm().valueStr());
			}

			res.push_back(shelvedAlarm);
		}
	}

	session.switchDatastore(oldDs);
	return res;
}

bool matchShelf(const std::vector<ShelvedAlarm>& shelfConfig, const libyang::DataNode& node) {
	const auto& [alarmId, alarmQual, resource] = getKeys(node);
	for (const auto& entry : shelfConfig) {
		for (const auto& [shelfId, shelfQual] : entry.alarm_types) {
			if (alarmId == shelfId && alarmQual == shelfQual)
				return true;
		}
	}
	return false;
}

void removeNode(auto netconf, libyang::DataNode& edit, const char* path, const char* value) {
	auto [_x, delNode] = edit.newPath2(path, value);
	delNode->newMeta(*netconf, "operation", "remove");
}

void copyToShelf(libyang::DataNode& edit, const libyang::DataNode& alarm) {
	const auto& [alarmTypeId, alarmTypeQualifier, resource] = getKeys(alarm);

	auto key = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[alarm-type-id='"s + alarmTypeId + "'][alarm-type-qualifier='" + alarmTypeQualifier + "'][resource='" + resource + "']";
	edit.newPath(key.c_str(), nullptr, libyang::CreationOptions::Update);

	for (const auto& leafName : {"alarm-text", "is-cleared", "perceived-severity"}) {
		if (auto node = alarm.findPath(leafName)) {
			edit.newPath((key + "/" + leafName).c_str(), std::string(node.value().asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);
		}
	}
}

void copyFromShelf(libyang::DataNode& edit, const libyang::DataNode& alarm) {
	const auto& [alarmTypeId, alarmTypeQualifier, resource] = getKeys(alarm);

	auto key = "/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='"s + alarmTypeId + "'][alarm-type-qualifier='" + alarmTypeQualifier + "'][resource='" + resource + "']";
	edit.newPath(key.c_str(), nullptr, libyang::CreationOptions::Update);

	for (const auto& leafName : {"alarm-text", "is-cleared", "perceived-severity"}) {
		if (auto node = alarm.findPath(leafName)) {
			edit.newPath((key + "/" + leafName).c_str(), std::string(node.value().asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);
		}
	}
}

sysrepo::ErrorCode mngrUpdateControlCb(sysrepo::Session session, sysrepo::Event event, sysrepo::Connection& dataConn, sysrepo::Session& dataSess) {
	if (event != sysrepo::Event::Done) {
		return sysrepo::ErrorCode::Ok;
	}
	auto shelf = fetchControlShelf(session);
	bool isChange = false;

	auto ctx = dataSess.getContext();
	auto netconf = ctx.getModuleImplemented("ietf-netconf");
	auto edit = ctx.newPath("/ietf-alarms:alarms", nullptr);

	if (auto data = dataSess.getData("/ietf-alarms:alarms")) {
		for (const auto& node : data->findXPath("/ietf-alarms:alarms/alarm-list/alarm")) {
			if (bool toBeShelved = matchShelf(shelf, node)) {
				std::cout << "   SHELVING " << node.path() << std::endl;
				copyToShelf(edit, node);
				removeNode(netconf, edit, std::string(node.path()).c_str(), nullptr);
				isChange = true;
			}
		}

		for (const auto& node : data->findXPath("/ietf-alarms:alarms/shelved-alarms/shelved-alarm")) {
			if (bool toBeShelved = matchShelf(shelf, node); !toBeShelved) {
				std::cout << "   UNSHELVING " << node.path() << std::endl;
				copyFromShelf(edit, node);
				removeNode(netconf, edit, std::string(node.path()).c_str(), nullptr);
				isChange = true;
			}
		}
	}

	if (isChange) {
		dataSess.editBatch(edit, sysrepo::DefaultOperation::Merge);
		dataSess.applyChanges();
	}

	return sysrepo::ErrorCode::Ok;
}

bool activeAlarmExist(sysrepo::Session& session, const std::string& alarmId, const std::string& alarmQual, const std::string& resource, const std::vector<std::string>& paths) {
	for (const auto& path : paths) {
		if (auto rootNode = session.getData("/ietf-alarms:alarms")) {
			for (const auto& node : rootNode->findXPath(path.c_str())) {
				if (getActive(node)) {
					return true;
				}
			}
		}
	}

	return false;
}

sysrepo::ErrorCode mngrRPC(sysrepo::Session session, sysrepo::Event event, const libyang::DataNode input, libyang::DataNode output, sysrepo::Connection& dataConn, sysrepo::Session& dataSess) {
	const auto& [id, qual, res] = getKeys(input);
	const auto active = getActive(input);

	auto shelf = fetchControlShelf(session);

	const auto prefixShelved = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[alarm-type-id='"s + id + "'][alarm-type-qualifier='" + qual + "'][resource='" + res + "']";
	const auto prefixUnshelved = "/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='"s + id + "'][alarm-type-qualifier='" + qual + "'][resource='" + res + "']";

	const std::string& prefix = matchShelf(shelf, input) ? prefixShelved : prefixUnshelved;

	// if passing is-cleared=true alarm and THAT alarm does not exist or exists but inactive (is-cleared=true), do nothing, it's a NOOP
	if (auto exists = activeAlarmExist(dataSess, id, qual, res, {prefixUnshelved, prefixShelved}); !exists && !active) {
		return sysrepo::ErrorCode::Ok;
	}

	dataSess.setItem(prefix.c_str(), nullptr);
	for (const auto& node : input.childrenDfs()) {
		if (auto nodeName = node.schema().name(); node.isTerm() && nodeName != "alarm-type-qualifier" && nodeName != "alarm-type-id" && nodeName != "resource") {
			dataSess.setItem((prefix + "/" + std::string(nodeName)).c_str(), std::string(node.asTerm().valueStr()).c_str());
		}
	}

	dataSess.applyChanges();

	return sysrepo::ErrorCode::Ok;
}
