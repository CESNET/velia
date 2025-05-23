#include <docopt.h>
#include <sdbus-c++/IProxy.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "VELIA_VERSION.h"
#include "main.h"
#include "system_vars.h"
#include "system/Authentication.h"
#include "system/Firmware.h"
#include "system/IETFInterfaces.h"
#include "system/IETFInterfacesConfig.h"
#include "system/IETFSystem.h"
#include "system/JournalUpload.h"
#include "system/LED.h"
#include "system/LLDP.h"
#include "system/LLDPCallback.h"
#include "utils/exceptions.h"
#include "utils/exec.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/sysrepo.h"

using namespace std::literals;

static const char usage[] =
    R"(Sysrepo-powered system management.

Usage:
  veliad-system
    [--main-log-level=<Level>]
    [--sysrepo-log-level=<Level>]
    [--system-log-level=<Level>]
  veliad-system (-h | --help)
  veliad-system --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --main-log-level=<N>              Log level for other messages [default: 2]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 2]
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
    velia::utils::initLogsSysrepo();
    spdlog::set_level(spdlog::level::info);

    spdlog::get("main")->set_level(parseLogLevel("other messages", args["--main-log-level"]));
    spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));
    spdlog::get("system")->set_level(parseLogLevel("System logging", args["--system-log-level"]));

    auto srConn = sysrepo::Connection{};
    auto srSess = srConn.sessionStart();

    DBUS_EVENTLOOP_START

    auto journalUploadStartup = velia::system::JournalUpload(srConn.sessionStart(sysrepo::Datastore::Startup), "/cfg/journald-remote", [](auto) {});
    auto journalUploadRunning = velia::system::JournalUpload(srConn.sessionStart(sysrepo::Datastore::Running), "/run/journald-remote", [dbusConn = g_dbusConnection.get()](auto log) {
        log->debug("Restarting systemd-journal-upload.service");
        auto sdManager = sdbus::createProxy(*dbusConn, "org.freedesktop.systemd1", "/org/freedesktop/systemd1");
        sdManager->callMethod("RestartUnit").onInterface("org.freedesktop.systemd1.Manager").withArguments("systemd-journal-upload.service"s, "replace"s);
    });

    // initialize ietf-system
    auto sysrepoIETFSystem = velia::system::IETFSystem(srSess, "/etc/os-release", *g_dbusConnection, "org.freedesktop.resolve1");

    auto dbusConnection = sdbus::createConnection(); // second connection for RAUC (for calling methods).
    dbusConnection->enterEventLoopAsync();

    // implements ietf-interfaces and ietf-routing
    const std::filesystem::path runtimeNetworkDirectory("/run/systemd/network"), persistentNetworkDirectory("/cfg/network/");
    std::filesystem::create_directories(runtimeNetworkDirectory);
    std::filesystem::create_directories(persistentNetworkDirectory);
    auto srSessStartup = srConn.sessionStart(sysrepo::Datastore::Startup);

    // IMPORTANT: This list MUST be kept aligned with:
    // - yang/czechlight-network@*.yang
    // - CzechLight/br2-external's board/czechlight/clearfog/overlay/usr/lib/systemd/network/*.network
    //
    // ...otherwise Bad Things™ happen.
    std::vector<std::string> managedLinks = {"br0", "eth0", "eth1", "eth2", "osc", "oscE", "oscW", "sfp3"};

    auto sysrepoIETFInterfacesOperational = std::make_shared<velia::system::IETFInterfaces>(srSess);
    auto sysrepoIETFInterfacesStartup = velia::system::IETFInterfacesConfig(srSessStartup, persistentNetworkDirectory, managedLinks, [](const auto&) {});
    auto sysrepoIETFInterfacesRunning = velia::system::IETFInterfacesConfig(srSess, runtimeNetworkDirectory, managedLinks, [](const auto& reconfiguredInterfaces) {
        auto log = spdlog::get("system");

        /* Bring all the updated interfaces down
         *
         * This is required at least when transitioning from bridge to DHCP configuration. systemd-networkd apparently does not reset many
         * interface properties when reconfiguring the interface into new "bridge-less" configuration (the interface stays in the
         * bridge and it also does not obtain link local address).
         */
        for (const auto& interfaceName : reconfiguredInterfaces.deleted) {
            try {
                velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"down", interfaceName}, "");
            } catch (std::runtime_error& e) {
                log->warn("velia-system: IETFInterfacesConfig: cannot bring down {}: {}", interfaceName, e.what());
            }
        }
        for (const auto& interfaceName : reconfiguredInterfaces.changedOrNew) {
            try {
                velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"down", interfaceName}, "");
            } catch (std::runtime_error& e) {
                log->warn("velia-system: IETFInterfacesConfig: cannot bring down {}: {}", interfaceName, e.what());
            }
        }

        velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"reload"}, "");

        // Let's also explicitly bring all interfaces which are expected to have "some" configuration back up again.
        // This was needed at least on the "oscW" and "oscE" interfaces on in-line amplifiers in May 2025.
        // I have no idea how come that this affects this pair of interfaces, but it has no effect on the "osc"
        // one on ROADM Line Degree boxes. Let's just bring them up explicitly.
        for (const auto& interfaceName : reconfiguredInterfaces.changedOrNew) {
            velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"up", interfaceName}, "");
        }
    });

    auto sysrepoFirmware = velia::system::Firmware(srConn, *g_dbusConnection, *dbusConnection);

    auto srSess2 = srConn.sessionStart();
    auto authentication = velia::system::Authentication(srSess2, REAL_ETC_PASSWD_FILE, REAL_ETC_SHADOW_FILE, AUTHORIZED_KEYS_FORMAT, velia::system::impl::changePassword);

    auto leds = velia::system::LED(srConn, "/sys/class/leds");

    auto lldp = std::make_shared<velia::system::LLDPDataProvider>([]() { return velia::utils::execAndWait(spdlog::get("system"), NETWORKCTL_EXECUTABLE, {"lldp", "--json=short"}, ""); });
    auto srSubs = srSess.onOperGet("czechlight-lldp", velia::system::LLDPCallback(lldp), "/czechlight-lldp:nbr-list");

    DBUS_EVENTLOOP_END
    return 0;
}
