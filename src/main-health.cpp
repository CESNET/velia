#include <docopt.h>
#include <future>
#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include "VELIA_VERSION.h"
#include "health/Factory.h"
#include "health/inputs/DbusSystemdInput.h"
#include "health/manager/StateManager.h"
#include "health/outputs/callables.h"
#include "main.h"
#include "utils/exceptions.h"
#include "utils/journal.h"
#include "utils/log.h"
#include "utils/log-init.h"

static const char usage[] =
    R"(Monitor system health status.

Usage:
  veliad-health
    [--appliance=<Model>]
    [--log-level=<Level>]
    [--health-log-level=<Level>]
    [--systemd-ignore-unit=<Unit>]...
  veliad-health (-h | --help)
  veliad-health --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --log-level=<N>                   Log level for everything [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --health-log-level=<N>            Log level for the health monitoring
  --systemd-ignore-unit=<Unit>      Ignore state of systemd's unit in systemd state tracker. Can be specified multiple times.
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

    try {
        spdlog::set_level(parseLogLevel("Generic", args["--log-level"]));
        auto heLogLevel = args["--health-log-level"] ? args["--health-log-level"] : args["--log-level"];
        spdlog::get("health")->set_level(parseLogLevel("Health logging", heLogLevel));

        DBUS_EVENTLOOP_START

        // health
        auto manager = std::make_shared<velia::health::StateManager>();

        // output configuration
        if (const auto& appliance = args["--appliance"]) {
            spdlog::get("main")->debug("Initializing LED drivers");
            manager->m_outputSignal.connect(velia::health::createOutput(appliance.asString()));
        }

        spdlog::get("main")->debug("All outputs initialized.");

        // input configuration
        std::set<std::string> ignoredUnits(args["--systemd-ignore-unit"].asStringList().begin(), args["--systemd-ignore-unit"].asStringList().end());
        spdlog::get("main")->debug("Starting DBus systemd watcher");
        if (!ignoredUnits.empty()) {
            spdlog::get("main")->debug("Systemd input will ignore changes of the following units: {}", args["--systemd-ignore-unit"]);
        }
        auto inputSystemdDbus = std::make_shared<velia::health::DbusSystemdInput>(manager, ignoredUnits, *g_dbusConnection);

        spdlog::get("main")->debug("All inputs initialized.");

        DBUS_EVENTLOOP_END;

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
