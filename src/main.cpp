#include <docopt.h>
#include <future>
#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include "VELIA_VERSION.h"
#include "inputs/DbusSemaphoreInput.h"
#include "inputs/DbusSystemdInput.h"
#include "manager/StateManager.h"
#include "outputs/LedSysfsDriver.h"
#include "outputs/callables.h"
#include "utils/exceptions.h"
#include "utils/log-init.h"

/** @short Extract log level from a CLI option */
spdlog::level::level_enum parseLogLevel(const std::string& name, const docopt::value& option)
{
    long x;
    try {
        x = option.asLong();
    } catch (std::invalid_argument&) {
        throw std::runtime_error(name + " log level: expecting integer");
    }
    static_assert(spdlog::level::trace < spdlog::level::off, "spdlog::level levels have changed");
    static_assert(spdlog::level::off == 6, "spdlog::level levels have changed");
    if (x < 0 || x > 5)
        throw std::runtime_error(name + " log level invalid or out-of-range");

    return static_cast<spdlog::level::level_enum>(5 - x);
}

static const char usage[] =
    R"(Monitor system health status

Usage:
  veliad
    [--log-level=<Level>]
    [--manager-log-level=<Level>]
    [--input-log-level=<Level>]
    [--output-log-level=<Level>]
  veliad (-h | --help)
  veliad --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --log-level=<N>                   Log level for everything [default: 3]
  --manager-log-level=<N>           Log level for manager [default: 3]
  --input-log-level=<N>             Log level for the input providers [default: 3]
  --output-log-level=<N>            Log level for the output providers [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
)";

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    try {
        spdlog::set_level(parseLogLevel("Generic", args["--log-level"]));
        // nothing for "main", that just inherit the global stuff
        spdlog::get("manager")->set_level(parseLogLevel("Manager subsystem", args["--manager-log-level"]));
        spdlog::get("input")->set_level(parseLogLevel("Input subsystem", args["--input-log-level"]));
        spdlog::get("output")->set_level(parseLogLevel("Output subsystem", args["--output-log-level"]));
        // FIXME: the default values actually mean that nothing is propagated from the generic --log-level if passed...

        // open dbus connection and event loop
        spdlog::get("input")->trace("Opening DBus connection");
        auto dbusConnection = sdbus::createSystemBusConnection();
        std::thread eventLoop([&dbusConnection]() { dbusConnection->enterEventLoop(); });

        // register outputs and inputs
        auto manager = std::make_shared<velia::StateManager>();

        // output configuration
        spdlog::get("output")->trace("Initializing LED drivers");
        manager->m_outputSignal.connect(velia::LedOutputCallback(
            std::make_shared<velia::LedSysfsDriver>("/sys/class/leds/input3::scrolllock/"),
            std::make_shared<velia::LedSysfsDriver>("/sys/class/leds/input3::scrolllock/"),
            std::make_shared<velia::LedSysfsDriver>("/sys/class/leds/input3::scrolllock/")));

        spdlog::get("input")->trace("Starting DBus systemd watcher");
        // input configuration
        auto inputSystemdDbus = std::make_shared<velia::DbusSystemdInput>(manager, *dbusConnection);
        // auto inputSemaphoreDbus = std::make_shared<velia::DbusSemaphoreInput>(manager, *dbusConnection, "bus.name", "obj.path", "Semaphore", "propIface");

        eventLoop.join();
        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
