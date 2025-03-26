#include <boost/spirit/home/x3.hpp>
#include <docopt.h>
#include <fmt/format.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include "VELIA_VERSION.h"
#include "ietf-hardware/sysfs/IpmiFruEEPROM.h"
#include "ietf-hardware/sysfs/OnieEEPROM.h"
#include "utils/exceptions.h"
#include "utils/log-init.h"
#include "utils/log.h"

using namespace std::literals;

static const char usage[] =
    R"(Dump content of an IPMI FRU or ONIE EEPROM data

Usage:
  velia-eeprom [--ipmi | --onie] <i2c_bus> <i2c_address>
  velia-eeprom [--ipmi | --onie] <file>
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

std::string tlvType(const velia::ietf_hardware::sysfs::TLV::Type& type)
{
    using velia::ietf_hardware::sysfs::TLV;

    switch (type) {
    case TLV::Type::ProductName:
        return "Product name";
    case TLV::Type::PartNumber:
        return "P/N";
    case TLV::Type::SerialNumber:
        return "S/N";
    case TLV::Type::ManufactureDate:
        return "Manufacture date";
    case TLV::Type::DeviceVersion:
        return "Device version";
    case TLV::Type::Vendor:
        return "Vendor";
    case TLV::Type::VendorExtension:
        return "Vendor extension";
    default:
        return "Unknown field";
    }
}

template <class... Args>
void onieEeprom(Args&&... args)
{
    constexpr auto valueVisitor = [](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            return fmt::format("0x{:02x}", v);
        } else {
            return "";
        }
    };

    using velia::ietf_hardware::sysfs::TLV;
    const auto eepromData = velia::ietf_hardware::sysfs::onieEeprom(std::forward<Args>(args)...);

    for (const auto& entry : eepromData) {
        if (entry.type == TLV::Type::VendorExtension) {
            continue;
        }

        fmt::print("{}: {}\n", tlvType(entry.type), std::visit(valueVisitor, entry.value));
    }
}

template <class... Args>
void readEeprom(const docopt::Options& options, Args&&... args)
{
    if (options.at("--ipmi").asBool()) {
        ipmiFruEeprom(std::forward<Args>(args)...);
    } else if (options.at("--onie").asBool()) {
        onieEeprom(std::forward<Args>(args)...);
    } else {
        try {
            ipmiFruEeprom(std::forward<Args>(args)...);
            return;
        } catch (const std::exception& e) {
            spdlog::get("main")->debug("Failed to read IPMI FRU EEPROM: {}", e.what());
        }

        try {
            onieEeprom(std::forward<Args>(args)...);
            return;
        } catch (const std::exception& e) {
            spdlog::get("main")->debug("Failed to read ONIE EEPROM: {}", e.what());
        }

        throw std::runtime_error{"Failed to read any EEPROM"};
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
            readEeprom(args, std::filesystem::path{args["<file>"].asString()});
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
            readEeprom(args, std::filesystem::path{"/sys"}, bus, address);
        }

        return 0;
    } catch (std::exception& e) {
        velia::utils::fatalException(spdlog::get("main"), e, "main");
    }
}

