#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sysrepo-cpp/Subscription.hpp>
#include <unistd.h>

using namespace std::string_literals;

volatile sig_atomic_t g_exit_application = 0;

static const std::string MODULE_NAME = "ietf-hardware";
static const std::string MODULE_PREFIX = "/" + MODULE_NAME + ":hardware";

void valuesToYang(const std::map<std::string, std::string>& values, sysrepo::Session session, std::optional<libyang::DataNode>& parent, const std::string& prefix)
{
    for (const auto& [propertyName, value] : values) {
        if (!parent) {
            parent = session.getContext().newPath(prefix + propertyName, value, libyang::CreationOptions::Output);
        } else {
            parent->newPath(prefix + propertyName, value, libyang::CreationOptions::Output);
        }
    }
}

void usage(const char* progName)
{
    std::cout << "Usage: " << progName << "--subscribe|--setitem" << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    bool isDaemonSubscribe = argv[1] == "--subscribe"s;
    bool isDaemonSetItem = argv[1] == "--set-item"s;

    if (isDaemonSubscribe == isDaemonSetItem) {
        usage(argv[0]);
        return 1;
    }

    std::map<std::string, std::string> data;
    sysrepo::Connection srConn;
    auto srSess = srConn.sessionStart();
    std::optional<sysrepo::Subscription> srSub;

    if (isDaemonSubscribe) {
        data = {
            {"/component[name='ne']/description", "This data was brought to you by process 2 (subscr)."},
            {"/component[name='ne:ctrl']/class", "iana-hardware:module"},
        };

        sysrepo::OperGetCb cb = [&](sysrepo::Session session, auto, auto, auto, auto, auto, auto& parent) {
            valuesToYang(data, session, parent, MODULE_PREFIX);
            return sysrepo::ErrorCode::Ok;
        };


        srSub = srSess.onOperGet(
            MODULE_NAME,
            cb,
            MODULE_PREFIX + "/*",
            sysrepo::SubscribeOptions::Passive | sysrepo::SubscribeOptions::OperMerge);
    } else if (isDaemonSetItem) {
        data = {
            {"/component[name='ne']/class", "iana-hardware:module"},
            {"/component[name='ne:edfa']/class", "iana-hardware:module"},
        };

        srSess.switchDatastore(sysrepo::Datastore::Operational);
        for (const auto& [k, v] : data) {
            srSess.setItem(MODULE_PREFIX + k, v);
        }
        srSess.applyChanges();
        srSess.switchDatastore(sysrepo::Datastore::Running);
    }

    // touch a file so somebody can read that sysrepo things are initialised
    {
        std::string filename = std::to_string(getpid()) + ".sysrepo";
        std::ofstream ofs(filename);
        ofs << "";
    }

    signal(SIGTERM, [] (int){});
    pause();
    return 0;
}
