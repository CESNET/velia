#include <docopt.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Connection.hpp>
#include <unistd.h>
#include "VELIA_VERSION.h"
#include "firewall/Firewall.h"
#include "system_vars.h"
#include "utils/exceptions.h"
#include "utils/exec.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/log.h"
#include "utils/sysrepo.h"
#include "utils/waitUntilSignalled.h"

static const char usage[] =
    R"(Bridge between sysrepo and nftables.

Usage:
  veliad-firewall
    [--firewall-log-level=<Level>]
    [--main-log-level=<Level>]
    [--sysrepo-log-level=<Level>]
    [--nftables-include-file=<Path>]...
  veliad-firewall (-h | --help)
  veliad-firewall --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --firewall-log-level=<N>          Log level for the firewall [default: 3]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
  --main-log-level=<N>              Log level for other messages [default: 2]
  --sysrepo-log-level=<N>           Log level for the sysrepo library [default: 2]
  --nftables-include-file=<Path>    Files to include in the nftables config file.
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
    velia::utils::initLogsSysrepo();
    spdlog::set_level(spdlog::level::info);

    spdlog::get("firewall")->set_level(parseLogLevel("Firewall logging", args["--firewall-log-level"]));
    spdlog::get("main")->set_level(parseLogLevel("other messages", args["--main-log-level"]));
    spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo library", args["--sysrepo-log-level"]));

    std::vector<std::filesystem::path> nftIncludeFiles;
    for (const auto& path : args["--nftables-include-file"].asStringList()) {
        nftIncludeFiles.emplace_back(path);
    }

    auto srConn = sysrepo::Connection{};
    auto srSess = srConn.sessionStart();
    velia::firewall::SysrepoFirewall firewall(srSess, [] (const auto& config) {
        spdlog::get("firewall")->debug("running nft...");
        velia::utils::execAndWait(spdlog::get("firewall"), NFT_EXECUTABLE, {"-f", "-"}, config);

        spdlog::get("firewall")->debug("nft config applied.");
    }, nftIncludeFiles);

    waitUntilSignaled();

    return 0;
}
