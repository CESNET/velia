#include <docopt.h>
#include <iostream>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include "VELIA_VERSION.h"
#include "ietf-hardware/sysfs/EEPROM.h"
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

        std::string i2cBus = args["<i2c_bus>"].asString();
        std::string i2cAddress = args["<i2c_address>"].asString();

        const auto eepromData = velia::ietf_hardware::sysfs::eepromData(i2cBus, i2cAddress);

        std::cout << "I2C " << i2cBus << "-" << i2cAddress << " data:\n";
        std::cout << "Manufacturer: '" << eepromData.productInfo.manufacturer << "'\n";
        std::cout << "Product name: '" << eepromData.productInfo.name << "'\n";
        std::cout << "Part number: '" << eepromData.productInfo.partNumber << "'\n";
        std::cout << "Version: '" << eepromData.productInfo.version << "'\n";
        std::cout << "Serial number: '" << eepromData.productInfo.serialNumber << "'\n";
        std::cout << "Asset tag: '" << eepromData.productInfo.assetTag << "'\n";
        std::cout << "FRU file ID: '" << eepromData.productInfo.fruFileId << "'\n";
        std::cout << "Custom: " << '\n';
        for (const auto& custom : eepromData.productInfo.custom) {
            std::cout << " * '" << custom << "'\n";
        }

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}

