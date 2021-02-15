#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "VELIA_VERSION.h"
#include "main.h"
#include "system/Firmware.h"
#include "system/Authentication.h"
#include "system/Network.h"
#include "system_vars.h"
#include "system/IETFSystem.h"
#include "utils/exceptions.h"
#include "utils/exec.h"
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
    R"(Sysrepo-powered system management.

Usage:
  veliad-system
    [--log-level=<Level>]
    [--sysrepo-log-level=<Level>]
    [--system-log-level=<Level>]
  veliad-system (-h | --help)
  veliad-system --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --log-level=<N>                   Log level for everything [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 3]
  --system-log-level=<N>            Log level for the system stuff [default: 3]
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

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad-system " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    try {
        spdlog::set_level(parseLogLevel("Generic", args["--log-level"]));
        spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));

        spdlog::get("main")->debug("Opening Sysrepo connection");
        auto srConn = std::make_shared<sysrepo::Connection>();
        auto srSess = std::make_shared<sysrepo::Session>(srConn);

        DBUS_EVENTLOOP_START

        // initialize ietf-system
        spdlog::get("main")->debug("Initializing Sysrepo for system models");
        auto sysrepoIETFSystem = velia::system::IETFSystem(srSess, "/etc/os-release");

        auto dbusConnection = sdbus::createConnection(); // second connection for RAUC (for calling methods).
        dbusConnection->enterEventLoopAsync();

        // initialize czechlight-system:networking
        const std::filesystem::path runtimeNetworkDirectory("/run/systemd/network"), persistentNetworkDirectory("/cfg/network/");
        std::filesystem::create_directories(runtimeNetworkDirectory);
        std::filesystem::create_directories(persistentNetworkDirectory);
        auto sysrepoNetwork = velia::system::Network(srConn, runtimeNetworkDirectory, persistentNetworkDirectory, [](const std::vector<std::string>& reconfiguredInterfaces) {
            auto log = spdlog::get("system");

            /* Bring all the updated interfaces down (they will later be brought up by executing `networkctl reload`).
             *
             * This is required when transitioning from bridge to DHCP configuration. systemd-networkd apparently does not reset many
             * interface properties when reconfiguring the interface into new "bridge-less" configuration (the interface stays in the
             * bridge and it also does not obtain link local address).
             *
             * This doesn't seem to be required when transitioning from DHCP to bridge configuration. It's just a "precaution" because
             * there might be hidden some caveats that I am unable to see now (some leftover setting). Bringing the interface
             * down seems to reset the interface (and it is something we can afford in the interface reconfiguration process).
             */
            for (const auto& interfaceName : reconfiguredInterfaces) {
                velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"down", interfaceName}, "");
            }

            velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"reload"}, "");
        });

        auto sysrepoFirmware = velia::system::Firmware(srConn, *g_dbusConnection, *dbusConnection);

        auto srSess2 = std::make_shared<sysrepo::Session>(srConn);
        auto authentication = velia::system::Authentication(srSess2, REAL_ETC_PASSWD_FILE, REAL_ETC_SHADOW_FILE, AUTHORIZED_KEYS_FORMAT, velia::system::impl::changePassword);

        DBUS_EVENTLOOP_END
        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
