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
    R"(Dump content of an IPMI FRU EEPROM data

Usage:
  velia-eeprom <i2c_bus> <i2c_address>
  velia-eeprom <file>
  velia-eeprom (-h | --help)
  velia-eeprom --version

Options:
  -h --help                         Show this screen.
  --version                         Show version.
)";

template <class... Args>
void ipmiFruEeprom(Args&&... args)
{
    const auto eepromData = velia::ietf_hardware::sysfs::ipmiFruEeprom(std::forward<Args>(args)...);

    const auto& pi = eepromData.productInfo;
    fmt::print("Manufacturer: {}\nProduct name: {}\nP/N: {}\nVersion: {}\nS/N: {}\nAsset tag:{}\nFRU file ID: {}\n",
            pi.manufacturer, pi.name, pi.partNumber, pi.version, pi.serialNumber, pi.assetTag, pi.fruFileId);
    fmt::print("Custom: \n");
    for (const auto& custom : eepromData.productInfo.custom) {
        fmt::print(" * '{}'\n", custom);
    }
}

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    velia::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "velia-eeprom" VELIA_VERSION, true);

    try {
        velia::ietf_hardware::sysfs::FRUInformationStorage eepromData;
        if (args["<file>"]) {
            ipmiFruEeprom(std::filesystem::path{args["<file>"].asString()});
        } else {
            auto parseAddress = [](const std::string& input, const std::string& thing, const uint8_t min, const uint8_t max) -> uint8_t {
                namespace x3 = boost::spirit::x3;
                const auto num = x3::rule<struct _r, int>{} = (x3::lit("0x") >> x3::hex) | x3::int_;
                auto with_valid_range = [=](auto& ctx) {
                    x3::_pass(ctx) = x3::_attr(ctx) <= max && x3::_attr(ctx) >= min;
                };
                unsigned res; // this *must* be big enough to store the full range of X3's int_ and hex parsers
                if (!x3::parse(begin(input), end(input), num[with_valid_range], res)) {
                    throw std::runtime_error{
                        fmt::format("Cannot parse \"{}\" as {}: expected a decimal or hex number between {} and {}",
                                    input, thing, min, max)};
                }
                return res;
            };

            auto bus = parseAddress(args["<i2c_bus>"].asString(), "an I2C bus number", 0, 255);
            auto address = parseAddress(args["<i2c_address>"].asString(), "an I2C device address", 0x08, 0x77);
            ipmiFruEeprom(std::filesystem::path{"/sys"}, bus, address);
            fmt::print("IPMI FRU EEPROM at I2C bus {}, device {:#04x}:\n", bus, address);
        }

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}

