#include <boost/spirit/home/x3.hpp>
#include <docopt.h>
#include <fmt/format.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include "VELIA_VERSION.h"
#include "ietf-hardware/sysfs/IpmiFruEEPROM.h"
#include "utils/exceptions.h"
#include "utils/log-init.h"
#include "utils/log.h"

using namespace std::literals;

static const char usage[] =
    R"(Reads EEPROM data

Usage:
  velia-eeprom
    [--main-log-level=<Level>]
    [--hardware-log-level=<Level>]
    <i2c_bus> <i2c_address>
  velia-eeprom (-h | --help)
  velia-eeprom --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
  --hardware-log-level=<N>          Log level for the hardware drivers [default: 3]
  --main-log-level=<N>              Log level for other messages [default: 2]
                                    (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                                    4 -> debug, 5 -> trace)
)";

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "velia-eeprom" VELIA_VERSION, true);

    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    try {
        spdlog::get("hardware")->set_level(parseLogLevel("Hardware loggers", args["--hardware-log-level"]));
        spdlog::get("main")->set_level(parseLogLevel("other messages", args["--main-log-level"]));

        auto parseAddress = [](const std::string& input, const std::string& thing) {
            namespace x3 = boost::spirit::x3;
            auto it = begin(input);
            const auto parser = (x3::lit("0x") >> x3::hex) | x3::int_;
            uint8_t res;
            if (!x3::parse(it, end(input), parser, res)) {
                throw std::runtime_error(fmt::format("Cannot parse {} \"{}\": expected a decimal or hex number", thing, input));
            }
            return res;
        };

        auto bus = parseAddress(args["<i2c_bus>"].asString(), "I2C bus number");
        auto address = parseAddress(args["<i2c_address>"].asString(), "I2C device address");
        const auto eepromData = velia::ietf_hardware::sysfs::ipmiFruEeprom(std::filesystem::path{"/sys"}, bus, address);

        fmt::print("IPMI FRU EEPROM at I2C bus {}, device {:#02x}:\n", bus, address);
        const auto& pi = eepromData.productInfo;
        fmt::print("Manufactureer: {}\nProduct name: {}\nP/N: {}\nVersion: {}\nS/N: {}\nAsset tag:{}\nFRU file ID: {}\n",
                pi.manufacturer, pi.name, pi.partNumber, pi.version, pi.serialNumber, pi.assetTag, pi.fruFileId);
        fmt::print("Custom: \n");
        for (const auto& custom : eepromData.productInfo.custom) {
            fmt::print(" * '{}'\n", custom);
        }

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}

