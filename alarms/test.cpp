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

#include "daemon.h"
#include "client.h"

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


std::map<std::string, std::string> dump(const sysrepo::Session& sess, sysrepo::Datastore ds, const char* path) {
	std::map<std::string, std::string> res;
	auto oldds = sess.activeDatastore();
	sess.switchDatastore(ds);

	if (auto data = sess.getData(path); data) {
		for (const auto& node : data->childrenDfs()) {
			if (node.isTerm()) {
				res[std::string(node.path())] = node.asTerm().valueStr();
			}
		}
	}

	sess.switchDatastore(oldds);
	return res;
}

#define CREATE(conn, sess) \
	auto conn = std::make_shared<sysrepo::Connection>(); \
	auto sess = std::make_shared<sysrepo::Session>(conn->sessionStart());

#define DISCONNECT_AND_RESTORE(conn, sess) \
	sess.reset(); \
	conn.reset(); \
	conn = std::make_shared<sysrepo::Connection>(); \
	sess = std::make_shared<sysrepo::Session>(conn->sessionStart());

#define CTRL_SET(name, id, qualifier) ctrlSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='" name "']/alarm-type[alarm-type-id='" id "'][alarm-type-qualifier-match='" qualifier "']", nullptr);

#define CTRL_UNSET(name) ctrlSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='" name "']");

#define CLI_UPSERT_ALARM(sess, id, qualifier, resource, active, ...) invokeAlarm(*sess, id, qualifier, resource, active, {__VA_ARGS__});

TEST_CASE("1") {
	sysrepo::setLogLevelStderr(sysrepo::LogLevel::Information);
	libyang::setLogLevel(libyang::LogLevel::Warning);
	/* libyang::setLogOptions(libyang::LogOptions::Log | libyang::LogOptions::Store); */

	// reset datastore
	sysrepo::Connection{}.sessionStart().copyConfig(sysrepo::Datastore::Startup, "ietf-alarms", 1000ms);

	CREATE(mngrConn, mngrSess);
	CREATE(ctrlConn, ctrlSess);
	CREATE(cli1Conn, cli1Sess);
	CREATE(cli2Conn, cli2Sess);
	CREATE(userConn, userSess);

	mngrSess->switchDatastore(sysrepo::Datastore::Running);
	auto subCtrl = mngrSess->onModuleChange("ietf-alarms", [&mngrConn, &mngrSess](sysrepo::Session session, auto, auto, auto, sysrepo::Event event, auto) { return mngrUpdateControlCb(session, event, *mngrConn, *mngrSess); }, nullptr, 0, sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Passive);
	auto subRpc = mngrSess->onRPCAction("/czechlight-alarm-manager:create-or-update-alarm", [&mngrConn, &mngrSess](sysrepo::Session session, auto, auto, const libyang::DataNode input, sysrepo::Event event, auto, libyang::DataNode output) { return mngrRPC(session, event, input, output, *mngrConn, *mngrSess); });

	mngrSess->switchDatastore(sysrepo::Datastore::Operational);

	CTRL_SET("psu-disconnected", "czechlight-alarms:psu-alarm", "disconnected");
	ctrlSess->applyChanges();
	REQUIRE(dump(*userSess, sysrepo::Datastore::Running, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
			{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/alarm-type[alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier-match='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
			{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/alarm-type[alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier-match='disconnected']/alarm-type-qualifier-match", "disconnected"},
			{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/name", "psu-disconnected"},
			{"/ietf-alarms:alarms/control/max-alarm-status-changes", "32"},
			{"/ietf-alarms:alarms/control/notify-status-changes", "all-state-changes"},
			});

	SUBCASE("Create a single unshelved alarm from cli1") {
		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", true);

		SUBCASE("cli1 disconnection does not delete data") {
			cli1Sess.reset();
			cli1Conn.reset();
		}

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/is-cleared", "false"},
				});
	}

	SUBCASE("Create a single shelved alarm from cli1") {
		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", true);

		SUBCASE("cli1 disconnection does not delete data") {
			cli1Sess.reset();
			cli1Conn.reset();
		}

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
				});
	}

	SUBCASE("Create one shelved and one unshelved alarm from cli1") {
		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", true);
		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", true);

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/is-cleared", "false"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
				});

		SUBCASE("cli1 disconnection does not delete the data") {
			cli1Sess.reset();
			cli1Conn.reset();
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/is-cleared", "false"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
					});
		}

		SUBCASE("mngr disconnection deletes all the data") {
			mngrSess.reset();
			mngrConn.reset();
#if 0
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{});
#endif
		}
	}

	SUBCASE("Create one unshelved alarm, shelve it and unshelve again") {
		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", true);

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/is-cleared", "false"},
				});

		CTRL_SET("temperature-high", "czechlight-alarms:temperature-alarm", "high");
		ctrlSess->applyChanges();
		REQUIRE(dump(*userSess, sysrepo::Datastore::Running, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='temperature-high']/alarm-type[alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier-match='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='temperature-high']/alarm-type[alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
				{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='temperature-high']/name", "temperature-high"},

				{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/alarm-type[alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier-match='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/alarm-type[alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier-match='disconnected']/alarm-type-qualifier-match", "disconnected"},
				{"/ietf-alarms:alarms/control/alarm-shelving/shelf[name='psu-disconnected']/name", "psu-disconnected"},

				{"/ietf-alarms:alarms/control/max-alarm-status-changes", "32"},
				{"/ietf-alarms:alarms/control/notify-status-changes", "all-state-changes"},
				});

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/is-cleared", "false"},
				});

		CTRL_UNSET("temperature-high");
		ctrlSess->applyChanges();

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-id", "czechlight-alarms:temperature-alarm"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/resource", "edfa"},
				{"/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='czechlight-alarms:temperature-alarm'][alarm-type-qualifier='high']/is-cleared", "false"},
				});
	}

	SUBCASE("Create shelved alarm, then update its leafs (and move forth and forth+back)") {
		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", true);

		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
				});

		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", true, {"alarm-text", "text"}, {"perceived-severity", "warning"});
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-text", "text"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "warning"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
				});

		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", true, {"perceived-severity", "major"});
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-text", "text"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
				});

		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", false, {"perceived-severity", "major"});
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-text", "text"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "true"},
				});

		SUBCASE("Move forth") {
			CTRL_UNSET("psu-disconnected");
			ctrlSess->applyChanges();

			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-text", "text"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
					{"/ietf-alarms:alarms/alarm-list/alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "true"},
					});
		}

		SUBCASE("Move forth and back") {
			CTRL_UNSET("psu-disconnected");
			ctrlSess->applyChanges();
			CTRL_SET("psu-disconnected", "czechlight-alarms:psu-alarm", "disconnected");
			ctrlSess->applyChanges();

			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-text", "text"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "true"},
					});
		}
	}

	SUBCASE("Client sets alarm, disconnects, then connects again and clears the alarm") {
		CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", true, {"perceived-severity", "major"});
		REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
				{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
				});

		DISCONNECT_AND_RESTORE(cli1Conn, cli1Sess);

		SUBCASE("Clears the alarm") {
			CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", false);
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "true"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
					});

			SUBCASE("Sets the alarm back") {
				CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:psu-alarm", "disconnected", "psu-1", true, {"perceived-severity", "major"});
				REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
						{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
						{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
						{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
						{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
						{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
						});

			}
		}
		SUBCASE("Clears a non-existent alarm -> no-op") {
			CLI_UPSERT_ALARM(cli1Sess, "czechlight-alarms:temperature-alarm", "high", "edfa", false);
			REQUIRE(dump(*userSess, sysrepo::Datastore::Operational, "/ietf-alarms:alarms") == std::map<std::string, std::string>{
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-id", "czechlight-alarms:psu-alarm"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
					{"/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='psu-1'][alarm-type-id='czechlight-alarms:psu-alarm'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
					});
		}
	}
}
