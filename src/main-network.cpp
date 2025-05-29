#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "VELIA_VERSION.h"
#include "main.h"
#include "network/Factory.h"
#include "system_vars.h"
#include "utils/exceptions.h"
#include "utils/exec.h"
#include "utils/io.h"
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
    spdlog::get("network")->set_level(parseLogLevel("System logging", args["--network-log-level"]));

    auto srConn = sysrepo::Connection{};
    auto srSess = srConn.sessionStart(sysrepo::Datastore::Running);
    auto daemons = velia::network::create(
        srConn.sessionStart(sysrepo::Datastore::Startup),
        "/cfg/network/",
        srSess,
        "/run/systemd/network",
        // IMPORTANT: This list MUST be kept aligned with:
        // - yang/czechlight-network@*.yang
        // - CzechLight/br2-external's board/czechlight/clearfog/overlay/usr/lib/systemd/network/*.network
        //
        // ...otherwise Bad Things™ happen.
        {"br0", "eth0", "eth1", "eth2", "osc", "oscE", "oscW", "sfp3"},
        [](const auto&) {
            auto log = spdlog::get("network");

            /* In 2021, executing 'networkctl reload' was not enough. For bridge interfaces, we had to also bring the interface down and up.
             * As of 5/2025, it seems that bare 'networkctl reload' is sufficient.
             * Manpage of networkctl says that reload should be enough except for few cases (like changing VLANs etc.), but they said that in 2021 too.
             * */
            velia::utils::execAndWait(log, NETWORKCTL_EXECUTABLE, {"reload"}, "");
        },
        []() { return velia::utils::execAndWait(spdlog::get("network"), NETWORKCTL_EXECUTABLE, {"lldp", "--json=short"}, ""); },
        velia::network::LLDPDataProvider::LocalData{
            .chassisId = velia::utils::readFileString("/etc/machine-id"),
            .chassisSubtype = "local"}
    );

    auto srSubs = srSess.onOperGet("czechlight-lldp", daemons.lldp, "/czechlight-lldp:nbr-list");

return 0;
}
