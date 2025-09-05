#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "VELIA_VERSION.h"
#include "main.h"
#include "network/Factory.h"
#include "network/NetworkctlUtils.h"
#include "system_vars.h"
#include "utils/exceptions.h"
#include "utils/exec.h"
#include "utils/io.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/sysrepo.h"
#include "utils/waitUntilSignalled.h"

using namespace std::literals;

static const char usage[] =
    R"(Sysrepo-powered network management.

Usage:
  veliad-network
    [--main-log-level=<Level>]
    [--sysrepo-log-level=<Level>]
    [--network-log-level=<Level>]
  veliad-network (-h | --help)
  veliad-network --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --main-log-level=<N>              Log level for other messages [default: 2]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 3]
  --network-log-level=<N>           Log level for the network stuff [default: 3]
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

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad-network " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    velia::utils::initLogsSysrepo();
    spdlog::set_level(spdlog::level::info);

    spdlog::get("main")->set_level(parseLogLevel("other messages", args["--main-log-level"]));
    spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));
    spdlog::get("network")->set_level(parseLogLevel("Network logging", args["--network-log-level"]));

    const std::filesystem::path runtimeConfigDirectory = "/run/systemd/network";
    const std::filesystem::path systemdConfigDirectory = "/usr/lib/systemd/network";
    const auto managedLinks = velia::network::systemdNetworkdManagedLinks(velia::utils::execAndWait(spdlog::get("network"), NETWORKCTL_EXECUTABLE, {"list", "--json=short"}, ""));

    auto daemons = velia::network::create(
        sysrepo::Connection{},
        "/cfg/network/",
        runtimeConfigDirectory,
        // IMPORTANT: veliad-network will only configure those interfaces which are "managed by systemd-networkd"
        // at the time this code starts up. In practice, this means that this code does not support dynamic hotplug
        // of interfaces, and that there MUST be exactly one `foo.network` for each of the managed interfaces, and
        // that it's base name matches the name of the interface exactly.
        //
        // On CzechLight devices, this is taken care of by CzechLight/br2-external's "factory defaults" in
        // board/czechlight/clearfog/overlay/usr/lib/systemd/network/*.network, and by CzechLight/br2-external's
        // package/czechlight-cfg-fs/cfg-restore-systemd-networkd.service which copies stuff from /usr (with
        // factory-defaults), and later from /cfg (where we pre-generate them from the startup DS) into /run
        // (where we store stuff from the running DS).
        managedLinks,
        [runtimeConfigDirectory, systemdConfigDirectory, managedLinks = std::set<std::string>{managedLinks.begin(), managedLinks.end()}](const auto&) {
            auto log = spdlog::get("network");

            /* In 2021, executing 'networkctl reload' was not enough. For bridge interfaces, we had to also bring the interface down and up.
             * As of 5/2025, it seems that bare 'networkctl reload' is sufficient.
             * Manpage of networkctl says that reload should be enough except for few cases (like changing VLANs etc.), but they said that in 2021 too.
             * */
            velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"reload"}, "");

            const auto statusJson = velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"status", "--json=short"}, "Reloading systemd-networkd configuration");
            for (const auto& [linkName, confFiles] : velia::network::linkConfigurationFiles(statusJson, managedLinks)) {
                const std::string confFilename = "10-"s + linkName + ".network";

                if (!confFiles.networkFile) {
                    log->error("Did not find a configuration file for systemd-networkd managed link {}", linkName);
                } else if (confFiles.networkFile != runtimeConfigDirectory / confFilename && confFiles.networkFile != systemdConfigDirectory / confFilename) {
                    log->error("Unexpected configuration file for link {}: {}", linkName, confFiles.networkFile->string());
                }

                if (!confFiles.dropinFiles.empty()) {
                    log->error("Unexpected drop-in configuration files for link {}", linkName);
                }
            }
        },
        []() { return velia::utils::execAndWait(spdlog::get("network"), NETWORKCTL_EXECUTABLE, {"lldp", "--json=short"}, ""); },
        velia::network::LLDPDataProvider::LocalData{
            .chassisId = velia::utils::readFileString("/etc/machine-id"),
            .chassisSubtype = "local"}
    );
    waitUntilSignaled();
    return 0;
}
