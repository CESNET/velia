#include <csignal>
#include <docopt.h>
#include <future>
#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "Factory.h"
#include "VELIA_VERSION.h"
#include "health/inputs/DbusSystemdInput.h"
#include "health/manager/StateManager.h"
#include "health/outputs/callables.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "utils/exceptions.h"
#include "utils/journal.h"
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
    R"(Monitor system health status.

Usage:
  veliad
    [--appliance=<Model>]
    [--log-level=<Level>]
    [--systemd-ignore-unit=<Unit>]...
  veliad (-h | --help)
  veliad --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --appliance=<Model>               Initialize IETF Hardware and outputs for specific appliance.
  --log-level=<N>                   Log level for everything [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --systemd-ignore-unit=<Unit>      Ignore state of systemd's unit in systemd state tracker. Can be specified multiple times.
)";

std::unique_ptr<sdbus::IConnection> g_dbusConnection;

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink;
    if (velia::utils::isJournaldActive()) {
        loggingSink = velia::utils::create_journald_sink();
    } else {
        loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    }

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    std::thread eventLoop;

    try {
        spdlog::set_level(parseLogLevel("Generic", args["--log-level"]));
        spdlog::get("main")->debug("Opening DBus connection");
        g_dbusConnection = sdbus::createSystemBusConnection();

        spdlog::get("main")->debug("Opening Sysrepo connection");
        auto srConn = std::make_shared<sysrepo::Connection>();
        auto srSess = std::make_shared<sysrepo::Session>(srConn);
        auto srSubscription = std::make_shared<sysrepo::Subscribe>(srSess);

        // initialize ietf-hardware
        spdlog::get("main")->debug("Initializing IETFHardware module");
        std::shared_ptr<velia::ietf_hardware::IETFHardware> hardwareState;
        if (const auto& appliance = args["--appliance"]) {
            hardwareState = velia::ietf_hardware::createIETFHardware(appliance.asString());
        }

        spdlog::get("main")->debug("Initializing Sysrepo ietf-hardware callback");
        auto ietfHardware = velia::ietf_hardware::sysrepo::Sysrepo(srSubscription, hardwareState);

        // Gracefully leave dbus event loop on SIGTERM
        struct sigaction sigact;
        memset(&sigact, 0, sizeof(sigact));
        sigact.sa_handler = [](int) { g_dbusConnection->leaveEventLoop(); }; // sdbus-c++'s implementation doesn't mind if called before entering the event loop. It simply leaves the loop on entry
        sigact.sa_flags = SA_SIGINFO;
        sigaction(SIGTERM, &sigact, nullptr);

        spdlog::get("main")->debug("Starting DBus event loop");
        eventLoop = std::thread([] { g_dbusConnection->enterEventLoop(); });

        // health
        auto manager = std::make_shared<velia::health::StateManager>();

        // output configuration
        spdlog::get("main")->debug("Initializing LED drivers");
        if (const auto& appliance = args["--appliance"]) {
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

        eventLoop.join();

        spdlog::get("main")->debug("Shutting down");
        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
