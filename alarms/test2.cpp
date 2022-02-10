#include <libyang-cpp/DataNode.hpp>
#include <libyang-cpp/Enum.hpp>
#include <iostream>
#include <libyang-cpp/Utils.hpp>
#include <thread>
#include <type_traits>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/utils/utils.hpp>

using namespace std::chrono_literals;

sysrepo::ErrorCode updateCb(sysrepo::Session session, unsigned int, std::string_view, std::optional<std::string_view>, sysrepo::Event event, unsigned int) {
	if (event == sysrepo::Event::Done) {
		return sysrepo::ErrorCode::Ok;
	}

	auto ctx = session.getContext();
	auto edit = ctx.newPath("/ietf-interfaces:interfaces", nullptr);
	auto netconf = ctx.getModuleImplemented("ietf-netconf");

	auto changes = session.getChanges("/ietf-interfaces:interfaces/interface//.");
	for (const auto& change: changes) {
		std::cout << "      CB: " << change.operation << ", " << change.node.path() << std::endl;
		if (change.operation == sysrepo::ChangeOperation::Created && change.node.schema().path() == "/ietf-interfaces:interfaces/interface/oper-status") {
			edit.newPath("/ietf-interfaces:interfaces/interface[name='lo']/oper-status", std::string(change.node.asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);

			/* auto [_x, delNode] = edit.newPath2(std::string(change.node.path()).c_str(), std::string(change.node.asTerm().valueStr()).c_str()); */
			/* delNode->newMeta(*netconf, "operation", "remove"); */
		}
		else if (change.operation == sysrepo::ChangeOperation::Deleted && change.node.schema().path() == "/ietf-interfaces:interfaces/interface") {
			std::cout << "      * list instance delete, updating statistics " << std::endl;
			edit.newPath("/ietf-interfaces:interfaces/interface[name='ahoj']/statistics/in-octets", "1", libyang::CreationOptions::Update);
		}
	}

	session.editBatch(edit, sysrepo::DefaultOperation::Merge);

	return sysrepo::ErrorCode::Ok;
}

int main(int argc, char* argv[])
{
#if 1
	sysrepo::setLogLevelStderr(sysrepo::LogLevel::Debug);
#endif
#if 0
	libyang::setLogLevel(libyang::LogLevel::Verbose);
	libyang::setLogOptions(libyang::LogOptions::Log | libyang::LogOptions::Store);
#endif

	auto conn1 = sysrepo::Connection{};
	auto sess1 = conn1.sessionStart();

	sess1.switchDatastore(sysrepo::Datastore::Operational);
	sess1.setItem("/ietf-interfaces:interfaces/interface[name='ahoj']/statistics/in-octets", "0");
	sess1.setItem("/ietf-interfaces:interfaces/interface[name='ahoj']/statistics/in-errors", "0");
	sess1.applyChanges();

	auto sub = sess1.onModuleChange("ietf-interfaces", updateCb, nullptr, 0, sysrepo::SubscribeOptions::Update | sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Passive);

	{
		auto conn2 = sysrepo::Connection{};
		auto sess2 = conn2.sessionStart();

		sess2.switchDatastore(sysrepo::Datastore::Operational);
		sess2.setItem("/ietf-interfaces:interfaces/interface[name='eth0']", nullptr);
		sess2.setItem("/ietf-interfaces:interfaces/interface[name='eth0']/oper-status", "up");
		sess2.setItem("/ietf-interfaces:interfaces/interface[name='eth1']", nullptr);
		sess2.setItem("/ietf-interfaces:interfaces/interface[name='eth1']/oper-status", "down");
		sess2.applyChanges();

		std::cout << "DESTROYING" << std::endl;
	}
	std::cout << "DEAD" << std::endl;

	std::cout << "OPER DS CONTENTS:" << std::endl;
	if (auto data = sess1.getData("//."); data) {
		for (const auto& node : data->childrenDfs()) {
			if (node.isTerm()) {
				std::cout << " ->" << node.path() << " = " << node.asTerm().valueStr() << std::endl;
			}
		}
	}

	return 0;
}
