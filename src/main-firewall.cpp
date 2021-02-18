#include <boost/process.hpp>
#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include <unistd.h>
#include "VELIA_VERSION.h"
#include "firewall/Firewall.h"
#include "system_vars.h"
#include "utils/exceptions.h"
#include "utils/exec.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/log.h"
#include "utils/waitUntilSignalled.h"

static const char usage[] =
    R"(Bridge between sysrepo and nftables.

Usage:
  veliad-firewall
    [--sysrepo-log-level=<Level>]
    [--firewall-log-level=<Level>]
  veliad-firewall (-h | --help)
  veliad-firewall --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --firewall-log-level=<N>          Log level for the firewall [default: 3]
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 2]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
)";

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink;
    if (velia::utils::isJournaldActive()) {
        loggingSink = velia::utils::create_journald_sink();
    } else {
        loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    }

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "veliad-firewall " VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    try {
        spdlog::get("firewall")->set_level(parseLogLevel("Firewall logging", args["--firewall-log-level"]));
        spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));

        auto srConn = std::make_shared<sysrepo::Connection>();
        auto srSess = std::make_shared<sysrepo::Session>(srConn);
        velia::firewall::SysrepoFirewall firewall(srSess, [] (const auto& config) {
            spdlog::get("firewall")->debug("running nft...");
            velia::utils::execAndWait(spdlog::get("firewall"), NFT_EXECUTABLE, {"-f", "-"}, config);

            spdlog::get("firewall")->debug("nft config applied.");
        });

        waitUntilSignaled();

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
