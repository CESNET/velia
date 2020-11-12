#include <csignal>
#include <docopt/docopt.h>
#include <spdlog/details/registry.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "VELIA_VERSION.h"
#include "ietf-hardware/IETFHardware.h"
#include "sysrepo/OpsCallback.h"
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
    R"(Report hardware state through Sysrepo

Usage:
  velia-hardwarestated
    [--log-level=<Level>]
    [--sysrepo-log-level=<Level>]
    [--hardware-log-level=<Level>]
  velia-hardwarestated (-h | --help)
  velia-hardwarestated --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --log-level=<N>                   Log level for everything [default: 3]
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 3]
  --hardware-log-level=<N>          Log level for the hardware drivers [default: 3]
)";

volatile sig_atomic_t g_exit_application = 0;

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink;
    if (velia::utils::isJournaldActive()) {
        loggingSink = velia::utils::create_journald_sink();
    } else {
        loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    }

    auto args = docopt::docopt(usage,
                               {argv + 1, argv + argc},
                               true,
                               "velia-hardwarestated " VELIA_VERSION,
                               true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(parseLogLevel("Generic", args["--log-level"]));
    spdlog::get("hardware")->set_level(parseLogLevel("Hardware loggers", args["--hardware-log-level"]));
    spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));

    try {
        auto hwState = std::make_shared<velia::ietf_hardware::IETFHardware>();

        auto hwmonFans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-002e/hwmon/");
        auto sysfsTempFront = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1011100.i2c/i2c-1/1-002e/hwmon/");
        auto sysfsTempCpu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/virtual/thermal/thermal_zone0/");
        auto sysfsTempMII0 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:00/hwmon/");
        auto sysfsTempMII1 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:01/hwmon/");
        auto emmc = std::make_shared<velia::ietf_hardware::sysfs::EMMC>("/sys/block/mmcblk0/device/");

        hwState->registerComponent(velia::ietf_hardware::component::Controller("ne:ctrl", "ne"));
        hwState->registerComponent(velia::ietf_hardware::component::Fans("ne:fans", "ne", hwmonFans, 4));
        hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
        hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
        hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
        hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
        hwState->registerComponent(velia::ietf_hardware::component::EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

        spdlog::get("main")->debug("Initialized Hardware State module");

        auto conn = std::make_shared<sysrepo::Connection>();
        auto sess = std::make_shared<sysrepo::Session>(conn);
        auto subscribe = std::make_shared<sysrepo::Subscribe>(sess);
        spdlog::get("main")->debug("Initialized sysrepo connection");

        subscribe->oper_get_items_subscribe("ietf-hardware-state", velia::ietf_hardware::sysrepo::OpsCallback(hwState), "/ietf-hardware-state:hardware/*");
        spdlog::get("main")->debug("Initialized sysrepo callback");

        spdlog::get("main")->info("Started");

        /* Let's run forever in an infinite blocking loop. We have originally proposed something like
         * "while(!exit) sleep(big_number);" and expect that sleep(3) will get interrupted by SIGTERM.
         * However, such code is vulnerable to race-conditions. The SIGTERM could be received right after
         * the while condition is evaluated but before the sleep(3) was invoked.
         *
         * This can be solved using pselect and blocking signals.
         * See https://www.linuxprogrammingblog.com/all-about-linux-signals?page=6
         */

        // Install sighandler for SIGTERM
        struct sigaction sigact;
        memset(&sigact, 0, sizeof(sigact));
        sigact.sa_handler = [](int) { g_exit_application = 1; };
        sigact.sa_flags = SA_SIGINFO;
        sigaction(SIGTERM, &sigact, nullptr);

        // Block SIGTERM
        sigset_t sigset, oldset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGTERM);
        sigprocmask(SIG_BLOCK, &sigset, &oldset);

        while (!g_exit_application) {
            fd_set fd;
            FD_ZERO(&fd);

            // if SIGTERM received at this point, it is deffered until pselect is entered which enables the signal processing again
            pselect(0, &fd, NULL, NULL, NULL, &oldset);
        }

        spdlog::get("main")->info("Shutting down");
        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::details::registry::instance().default_logger(), e, "main");
    }
}
