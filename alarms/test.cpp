#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>
#include <libyang-cpp/DataNode.hpp>
#include <libyang-cpp/Enum.hpp>
#include <iostream>
#include <libyang-cpp/Utils.hpp>
#include <map>
#include <memory>
#include <thread>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/utils/utils.hpp>

using namespace std::chrono_literals;
using namespace std::string_literals;

std::ostream& operator<< (std::ostream& os, const std::map<std::string, std::string>& value) {
	os << "{" << std::endl;
	for (const auto& [k, v] : value) {
		os << "  {" << k << ", " << v << "},\n";
	}
    return os << "}";
}

namespace doctest {
    template<> struct StringMaker<std::map<std::string, std::string>> {
        static String convert(const std::map<std::string, std::string>& value) {
			std::ostringstream os;

			os << "{" << std::endl;
			for (const auto& [k, v] : value) {
				os << "  {" << k << ", " << v << "},\n";
			}
			os << "}";
			return os.str().c_str();
		}
    };
}

std::tuple<std::string, std::string, std::string> getKeys(const libyang::DataNode& node) {
	auto type =     std::string(node.findPath("alarm-type-id").value().asTerm().valueStr());
	auto qual =     std::string(node.findPath("alarm-type-qualifier").value().asTerm().valueStr());
	auto resource = std::string(node.findPath("resource").value().asTerm().valueStr());
	return std::make_tuple(type, qual, resource);
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

sysrepo::ErrorCode updateCb(sysrepo::Session session, sysrepo::Event event /*, sysrepo::Connection& dataConn, sysrepo::Session& dataSess*/) {
	if (event == sysrepo::Event::Done) {
		return sysrepo::ErrorCode::Ok;
	}

	auto shelf = fetchControlShelf(session);

	auto ctx = session.getContext();
	auto netconf = ctx.getModuleImplemented("ietf-netconf");
	auto edit = ctx.newPath("/ietf-alarms:alarms", nullptr);
	bool isChange = false;

	auto changes = session.getChanges("//.");
	for (const auto& change: changes) {
		std::cout << "      CB: " << change.operation << ", " << change.node.path() << std::endl;
		if (change.operation == sysrepo::ChangeOperation::Created && change.node.schema().path() == "/ietf-alarms:alarms/alarm-list/alarm") {
			if (bool toBeShelved = matchShelf(shelf, change.node)) {
				const auto& [alarmTypeId, alarmTypeQualifier, resource] = getKeys(change.node);

				auto key = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[alarm-type-id='"s + alarmTypeId + "'][alarm-type-qualifier='" + alarmTypeQualifier + "'][resource='" + resource + "']";
				edit.newPath(key.c_str(), nullptr, libyang::CreationOptions::Update);

				removeNode(netconf, edit, std::string(change.node.path()).c_str(), nullptr);

				for (const auto& node : change.node.childrenDfs()) {
					if (node.isTerm()) {
						auto path = key + "/" + std::string(node.schema().name());
						edit.newPath(path.c_str(), std::string(node.asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);
					}
				}

				isChange = true;
			}
		}
	}

	if (isChange) {
		session.editBatch(edit, sysrepo::DefaultOperation::Merge);
	}

	return sysrepo::ErrorCode::Ok;
}

void copyToShelf(libyang::DataNode& edit, const libyang::DataNode& alarm) {
	const auto& [alarmTypeId, alarmTypeQualifier, resource] = getKeys(alarm);

	auto key = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[alarm-type-id='"s + alarmTypeId + "'][alarm-type-qualifier='" + alarmTypeQualifier + "'][resource='" + resource + "']";
	edit.newPath(key.c_str(), nullptr, libyang::CreationOptions::Update);

	if (auto alarmText = alarm.findPath("alarm-text")) {
		edit.newPath((key + "/alarm-text").c_str(), std::string(alarmText.value().asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);
	}
}

void copyFromShelf(libyang::DataNode& edit, const libyang::DataNode& alarm) {
	const auto& [alarmTypeId, alarmTypeQualifier, resource] = getKeys(alarm);

	auto key = "/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='"s + alarmTypeId + "'][alarm-type-qualifier='" + alarmTypeQualifier + "'][resource='" + resource + "']";
	edit.newPath(key.c_str(), nullptr, libyang::CreationOptions::Update);

	if (auto alarmText = alarm.findPath("alarm-text")) {
		edit.newPath((key + "/alarm-text").c_str(), std::string(alarmText.value().asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);
	}
}

sysrepo::ErrorCode ctrlUpdateCb(sysrepo::Session session, sysrepo::Event event, sysrepo::Connection& dataConn, sysrepo::Session& dataSess) {
	/* std::cout << "ctrlUpdateCb" << std::endl; */
	/* for (const auto& change : session.getChanges()) { */
	/* 	std::cout << "    " << change.operation << " " << change.node.path() << std::endl; */
	/* } */

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
			std::cout << "ctrl update MAYBE SHELVING? " << node.path() << std::endl;
			if (bool toBeShelved = matchShelf(shelf, node)) {
				std::cout << "   SHELVING " << node.path() << std::endl;
				copyToShelf(edit, node);
				removeNode(netconf, edit, std::string(node.path()).c_str(), nullptr);
				isChange = true;
			}
		}

		for (const auto& node : data->findXPath("/ietf-alarms:alarms/shelved-alarms/shelved-alarm")) {
			std::cout << "ctrl update MAYBE UNSHELVING? " << node.path() << std::endl;
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

std::map<std::string, std::string> dump(const sysrepo::Session& sess, sysrepo::Datastore ds) {
	std::map<std::string, std::string> res;
	auto oldds = sess.activeDatastore();

	if (auto data = sess.getData("//."); data) {
		for (const auto& node : data->childrenDfs()) {
			if (node.isTerm()) {
				res[std::string(node.path())] = node.asTerm().valueStr();
			}
		}
	}

	sess.switchDatastore(ds);
	return res;
}

std::pair<std::unique_ptr<sysrepo::Connection>, std::unique_ptr<sysrepo::Session>> init(std::optional<sysrepo::Datastore> ds = std::nullopt) {
	auto conn = std::make_unique<sysrepo::Connection>();
	auto sess = std::make_unique<sysrepo::Session>(conn->sessionStart());

	if (ds) {
		sess->switchDatastore(*ds);
	}

	return std::make_pair(std::move(conn), std::move(sess));
}

#define CTRL_SET(name, id, qualifier) ctrlSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='" name "']/alarm-type[alarm-type-id='" id "'][alarm-type-qualifier-match='" qualifier "']", nullptr);
#define CTRL_UNSET(name) ctrlSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='" name "']");
#define CLI_SET(sess, id, qualifier, resource) sess->setItem("/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='" id "'][alarm-type-qualifier='" qualifier "'][resource='" resource "']", nullptr);
#define CLI_SET_LEAF(sess, id, qualifier, resource, leaf, value) sess->setItem("/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='" id "'][alarm-type-qualifier='" qualifier "'][resource='" resource "']/" leaf, value);
#define CLI_DISCARD(conn, id, qualifier, resource) \
	conn->discardOperationalChanges("/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='" id "'][alarm-type-qualifier='" qualifier "'][resource='" resource "']"); \
	conn->discardOperationalChanges("/ietf-alarms:alarms/shelved-alarms/shelved-alarm[alarm-type-id='" id "'][alarm-type-qualifier='" qualifier "'][resource='" resource "']");

TEST_CASE("1") {
	sysrepo::setLogLevelStderr(sysrepo::LogLevel::Information);
	/* libyang::setLogLevel(libyang::LogLevel::Verbose); */
	/* libyang::setLogOptions(libyang::LogOptions::Log | libyang::LogOptions::Store); */

	// reset datastore
	sysrepo::Connection{}.sessionStart().copyConfig(sysrepo::Datastore::Startup, "ietf-alarms", 1000ms);

	auto cbConn = std::make_unique<sysrepo::Connection>();
	auto cbSess = std::make_unique<sysrepo::Session>(cbConn->sessionStart());

	auto [ctrlConn, ctrlSess] = init();
	auto [cli1Conn, cli1Sess] = init(sysrepo::Datastore::Operational);
	auto [cli2Conn, cli2Sess] = init(sysrepo::Datastore::Operational);
	auto [userConn, userSess] = init(sysrepo::Datastore::Operational);

	cbSess->switchDatastore(sysrepo::Datastore::Running);
	auto subCtrl = cbSess->onModuleChange("ietf-alarms", [&cbConn, &cbSess](sysrepo::Session session, auto, auto, auto, sysrepo::Event event, auto) { return ctrlUpdateCb(session, event, *cbConn, *cbSess); }, "/ietf-alarms:alarms/control", 0, sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Passive);

	cbSess->switchDatastore(sysrepo::Datastore::Operational);
	auto subList = cbSess->onModuleChange("ietf-alarms", [](sysrepo::Session session, auto, auto, auto, sysrepo::Event event, auto) { return     updateCb(session, event); }, "/ietf-alarms:alarms", 0, sysrepo::SubscribeOptions::Update | sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Passive);

	CTRL_SET("psu-disconnected", "czechlight-alarms:psu-alarm", "disconnected");
	ctrlSess->applyChanges();
	REQUIRE(dump(*ctrlSess, sysrepo::Datastore::Running) == std::map<std::string, std::string>{
			{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/alarm-type[alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier-match='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
			{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/alarm-type[alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier-match='disconnected']/alarm-type-qualifier-match", "disconnected"},
			{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/name", "psu-disconnected"},
			{"/ietf-alarms:alarms/control/max-alarm-status-changes", "32"},
			{"/ietf-alarms:alarms/control/notify-status-changes", "all-state-changes"},
	});
#if 0
	SUBCASE("Create and discard two alarms; one shelved") {
		CLI_SET(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "fan1"); // to be filtered
		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		cli1Sess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "fan1"},
				});

		SUBCASE("Erase shelved alarm") {
			CLI_DISCARD(cli1Conn, "czechlight-alarms:psu-alarm", "disconnected", "fan1");
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
					});
		}
		SUBCASE("Erase nonshelved alarm") {
			CLI_DISCARD(cli1Conn, "czechlight-alarms:temperature-alarm", "high", "edfa");
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "fan1"},
					});
		}
		SUBCASE("Erase both alarms") {
			CLI_DISCARD(cli1Conn, "czechlight-alarms:psu-alarm", "disconnected", "fan1");
			CLI_DISCARD(cli1Conn, "czechlight-alarms:temperature-alarm", "high", "edfa");
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{});
		}
	}

	SUBCASE("Create unshelved alarm, leaf modification") {
		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		cli1Sess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				});

		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", "alarm-text", "text");
		cli1Sess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},
				});

		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", "alarm-text", "text2");
		cli1Sess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text2"},
				});
	}

	SUBCASE("Create shelved alarm, modify its leaf") {
		CLI_SET(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "fan1");
		cli1Sess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "fan1"},
				});

		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "fan1", "alarm-text", "text");
		cli1Sess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "fan1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-text", "text"},
				});

		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "fan1", "alarm-text", "text-modified");
		cli1Sess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "fan1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='fan1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-text", "text-modified"},
				});
	}

	SUBCASE("Create unshelved alarms, change control, one alarm get gets shelved") {
		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", "alarm-text", "text");

		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "low", "edfa");
		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "low", "edfa", "alarm-text", "even longer text");

		cli1Sess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-text", "even longer text"},
				});

		CTRL_SET("temperature", "czechlight-alarms:temperature-alarm", "high");
		ctrlSess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-text", "even longer text"},
				});

		SUBCASE("Add more events to be shelved") {
			CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "wss");
			cli1Sess->applyChanges();

			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},

					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "wss"},

					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "edfa"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-text", "even longer text"},
					});
		}
	}

	SUBCASE("Create shelved alarms, unshelve") {
		CTRL_SET("temperature", "czechlight-alarms:temperature-alarm", "high");
		ctrlSess->applyChanges();

		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", "alarm-text", "text");
		cli1Sess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},
				});

		CTRL_UNSET("temperature");
		ctrlSess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},
				});
	}

	SUBCASE("Create two alarms; one shelved; then change shelf control so that first becomes unshelved and second shelved") {
		CTRL_SET("temp-hi", "czechlight-alarms:temperature-alarm", "high");
		ctrlSess->applyChanges();

		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", "alarm-text", "text");

		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "low", "wss");
		cli1Sess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},

				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "wss"},
				});

		CTRL_SET("temp-lo", "czechlight-alarms:temperature-alarm", "low");
		CTRL_UNSET("temp-hi");
		ctrlSess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},

				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "wss"},
				});
	}

	SUBCASE("Two clients; no moves") {
		CTRL_SET("temp-hi", "czechlight-alarms:temperature-alarm", "high");
		ctrlSess->applyChanges();

		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", "alarm-text", "text");

		CLI_SET(cli2Sess, "czechlight-alarms:temperature-alarm", "low", "wss");
		cli1Sess->applyChanges();
		cli2Sess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == dump(*cli2Sess, sysrepo::Datastore::Operational));
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},

				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "wss"},
				});

		cli2Sess.reset();
		cli2Conn.reset();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},
				});
	}

	SUBCASE("Create unshelved alarm, client disconnects") {
		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		cli1Sess->applyChanges();
		REQUIRE(cli1Sess->activeDatastore() == sysrepo::Datastore::Operational);
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				});

		cli1Sess.reset();
		cli1Conn.reset();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{});
	}

	SUBCASE("Create shelved alarm, client disconnects") {
		CTRL_SET("temp-hi", "czechlight-alarms:temperature-alarm", "high");
		ctrlSess->applyChanges();

		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		cli1Sess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				});

		cli1Sess.reset();
		cli1Conn.reset();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{ });
	}
#endif

	SUBCASE("Two clients; with moves") {
		CTRL_SET("temp-hi", "czechlight-alarms:temperature-alarm", "high");
		ctrlSess->applyChanges();

		CLI_SET(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa");
		CLI_SET_LEAF(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", "alarm-text", "text");
		cli1Sess->applyChanges();

		CLI_SET(cli2Sess, "czechlight-alarms:temperature-alarm", "low", "wss");
		cli2Sess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == dump(*cli2Sess, sysrepo::Datastore::Operational));
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},

				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "wss"},
				});

		SUBCASE("client 1 disconnects") {
			cli1Sess.reset();
			cli1Conn.reset(); // UPDATE CB: DELETE for my data invoked
			CHECK(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "wss"},
					});
		}
		SUBCASE("ctrl changes and client1 then disconnects") {
			CTRL_UNSET("temp-hi");
			ctrlSess->applyChanges();
		std::cout << "APPLIED CHANGES CTRL" << std::endl;
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-text", "text"},

					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "wss"},
					});

			std::cout << "DELETE" << std::endl;
			cli1Sess.reset();
			cli1Conn.reset();
			std::cout << "DELETE DONE" << std::endl;

			CHECK(dump(*userSess, sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='wss'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='low']/resource", "wss"},
					});
		}
	}
}
