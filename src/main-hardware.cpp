#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "VELIA_VERSION.h"
#include "ietf-hardware/Factory.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "utils/exceptions.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/waitUntilSignalled.h"

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
    R"(Hardware monitoring via Sysrepo.

Usage:
  veliad-hardware
    [--appliance=<Model>]
    [--log-level=<Level>]
    [--sysrepo-log-level=<Level>]
    [--hardware-log-level=<Level>]
  veliad-hardware (-h | --help)
  veliad-hardware --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --appliance=<Model>               Initialize IETF Hardware and outputs for specific appliance.
  --log-level=<N>                   Log level for everything [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 3]
  --hardware-log-level=<N>          Log level for the hardware drivers [default: 3]
)";

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink;
    if (velia::utils::isJournaldActive()) {
        loggingSink = velia::utils::create_journald_sink();
    } else {
        loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    }

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad-system " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    try {
        spdlog::set_level(parseLogLevel("Generic", args["--log-level"]));
        spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));
        spdlog::get("hardware")->set_level(parseLogLevel("Hardware loggers", args["--hardware-log-level"]));

        spdlog::get("main")->debug("Opening Sysrepo connection");
        auto srConn = std::make_shared<sysrepo::Connection>();
        auto srSess = std::make_shared<sysrepo::Session>(srConn);
        auto srSubscription = std::make_shared<sysrepo::Subscribe>(srSess);

        // initialize ietf-hardware
        spdlog::get("main")->debug("Initializing IETFHardware module");
        std::shared_ptr<velia::ietf_hardware::IETFHardware> ietfHardware;
        if (const auto& appliance = args["--appliance"]) {
            ietfHardware = velia::ietf_hardware::create(appliance.asString());
        } else {
            ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();
        }

        spdlog::get("main")->debug("Initializing Sysrepo ietf-hardware callback");
        auto sysrepoIETFHardware = velia::ietf_hardware::sysrepo::Sysrepo(srSubscription, ietfHardware);

        waitUntilSignaled();

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
