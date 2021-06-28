#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "VELIA_VERSION.h"
#include "main.h"
#include "system/Firmware.h"
#include "system/Authentication.h"
#include "system_vars.h"
#include "system/IETFInterfaces.h"
#include "system/IETFSystem.h"
#include "system/LED.h"
#include "utils/exceptions.h"
#include "utils/exec.h"
#include "utils/journal.h"
#include "utils/log.h"
#include "utils/log-init.h"
#include "utils/sysrepo.h"

static const char usage[] =
    R"(Sysrepo-powered system management.

Usage:
  veliad-system
    [--sysrepo-log-level=<Level>]
    [--system-log-level=<Level>]
  veliad-system (-h | --help)
  veliad-system --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 2]
  --system-log-level=<N>            Log level for the system stuff [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
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
    velia::utils::initLogsSysrepo();
    spdlog::set_level(spdlog::level::info);

    try {
        spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));
        spdlog::get("system")->set_level(parseLogLevel("System logging", args["--system-log-level"]));

        auto srConn = std::make_shared<sysrepo::Connection>();
        auto srSess = std::make_shared<sysrepo::Session>(srConn);

        DBUS_EVENTLOOP_START

        // initialize ietf-system
        auto sysrepoIETFSystem = velia::system::IETFSystem(srSess, "/etc/os-release");

        auto dbusConnection = sdbus::createConnection(); // second connection for RAUC (for calling methods).
        dbusConnection->enterEventLoopAsync();

        // implements ietf-interfaces and ietf-routing
        auto sysrepoIETFInterfaces = std::make_shared<velia::system::IETFInterfaces>(srSess);

        auto sysrepoFirmware = velia::system::Firmware(srConn, *g_dbusConnection, *dbusConnection);

        auto srSess2 = std::make_shared<sysrepo::Session>(srConn);
        auto authentication = velia::system::Authentication(srSess2, REAL_ETC_PASSWD_FILE, REAL_ETC_SHADOW_FILE, AUTHORIZED_KEYS_FORMAT, velia::system::impl::changePassword);

        auto leds = velia::system::LED(srConn, "/sys/class/leds");

        DBUS_EVENTLOOP_END
        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
