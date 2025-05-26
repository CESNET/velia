#include <docopt.h>
#include <future>
#include <health/outputs/AlarmsOutputs.h>
#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Connection.hpp>
#include "VELIA_VERSION.h"
#include "health/Factory.h"
#include "health/SystemdUnits.h"
#include "health/outputs/callables.h"
#include "main.h"
#include "utils/exceptions.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/log.h"

static const char usage[] =
    R"(Monitor system health status.

Usage:
  veliad-health
    [--appliance=<Model>]
    [--health-log-level=<Level>]
    [--main-log-level=<Level>]
    [--sysrepo-log-level=<Level>]
  veliad-health (-h | --help)
  veliad-health --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --health-log-level=<N>            Log level for the health monitoring [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --main-log-level=<N>              Log level for other messages [default: 2]
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 3]
)";

DBUS_EVENTLOOP_INIT

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink;
    if (velia::utils::isJournaldActive()) {
        loggingSink = velia::utils::create_journald_sink();
    } else {
        loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    }

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad-health " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    spdlog::get("health")->set_level(parseLogLevel("Health checker logger", args["--health-log-level"]));
    spdlog::get("main")->set_level(parseLogLevel("other messages", args["--main-log-level"]));
    spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));

    DBUS_EVENTLOOP_START

    auto srSessionAlarms = sysrepo::Connection{}.sessionStart();
    srSessionAlarms.switchDatastore(sysrepo::Datastore::Operational);

    // output configuration
    std::vector<std::function<void(velia::health::State)>> outputHandlers;
    if (const auto& appliance = args["--appliance"]) {
        spdlog::get("health")->debug("Initializing LED drivers");
        outputHandlers.emplace_back(velia::health::createOutput(appliance.asString()));
    }
    velia::health::AlarmsOutputs alarms(srSessionAlarms, outputHandlers);

    spdlog::get("health")->debug("All outputs initialized.");

    // input configuration
    spdlog::get("health")->debug("Starting DBus systemd units watcher");
    auto srSessionSystemdUnits = sysrepo::Connection{}.sessionStart();
    auto inputSystemdDbus = std::make_shared<velia::health::SystemdUnits>(srSessionSystemdUnits, *g_dbusConnection);

    DBUS_EVENTLOOP_END;

    return 0;
}
