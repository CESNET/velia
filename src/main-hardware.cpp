#include <chrono>
#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Connection.hpp>
#include "VELIA_VERSION.h"
#include "ietf-hardware/Factory.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "utils/exceptions.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/log.h"
#include "utils/sysrepo.h"
#include "utils/waitUntilSignalled.h"

static const char usage[] =
    R"(Hardware monitoring via Sysrepo.

Usage:
  veliad-hardware
    [--appliance=<Model>]
    [--hardware-log-level=<Level>]
    [--main-log-level=<Level>]
    [--sysrepo-log-level=<Level>]
  veliad-hardware (-h | --help)
  veliad-hardware --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --appliance=<Model>               Initialize IETF Hardware and outputs for specific appliance.
  --hardware-log-level=<N>          Log level for the hardware drivers [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --main-log-level=<N>              Log level for other messages [default: 2]
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 2]
)";

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink;
    if (velia::utils::isJournaldActive()) {
        loggingSink = velia::utils::create_journald_sink();
    } else {
        loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    }

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad-hardware " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    velia::utils::initLogsSysrepo();
    spdlog::set_level(spdlog::level::info);

    try {
        spdlog::get("hardware")->set_level(parseLogLevel("Hardware loggers", args["--hardware-log-level"]));
        spdlog::get("main")->set_level(parseLogLevel("other messages", args["--main-log-level"]));
        spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));

        auto srConn = sysrepo::Connection{};
        auto srSess = srConn.sessionStart();

        // initialize ietf-hardware
        std::shared_ptr<velia::ietf_hardware::IETFHardware> ietfHardware;
        if (const auto& appliance = args["--appliance"]) {
            ietfHardware = velia::ietf_hardware::create(appliance.asString());
        } else {
            ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();
        }

        auto sysrepoIETFHardware = velia::ietf_hardware::sysrepo::Sysrepo(srSess, ietfHardware, std::chrono::milliseconds{1500});

        waitUntilSignaled();

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
