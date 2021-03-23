#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "FspYhPsu.h"
#include "utils/log.h"
#include "utils/UniqueResource.h"

namespace {
bool i2cDevPresent(const std::string& devicePath, const uint8_t address)
{
    auto file = open(devicePath.c_str(), O_RDWR);
    if (file < 0) {
        throw std::runtime_error("i2cDevPresent: "s + strerror(errno));
    }

    auto fdClose = make_unique_resource([] {}, [file] {
        close(file);
    });

    if (ioctl(file, I2C_SLAVE_FORCE, address) < 0) {
        throw std::runtime_error("i2cDevPresent: "s + strerror(errno));
    }

    char bufferIn[1];
    return read(file, bufferIn, 1) != -1;
}

void i2cLoadDriver(const std::string& i2cDevice, const std::string& address)
{
    std::ofstream ofs("/sys/bus/i2c/devices/i2c-" + i2cDevice + "/new_device");
    ofs << "fsp3y_ym2151e " + address;
}

void i2cUnloadDriver(const std::string& i2cBus, const std::string& address)
{
    std::ofstream ofs("/sys/bus/i2c/devices/i2c-" + i2cBus + "/delete_device");
    ofs << address;
}
}

namespace velia::ietf_hardware {
FspYhPsu::FspYhPsu(const uint8_t i2cBus, const uint8_t i2cAddress, const std::filesystem::path& hwmonDir, const std::string& psuName)
    : m_i2cBus(i2cBus)
    , m_i2cAddress(i2cAddress)
    , m_hwmonDir(hwmonDir)
    , m_psuName(psuName)
{
    m_exit = false;
    m_psuWatcher = std::thread([this] {
        auto busString = std::to_string(m_i2cBus);
        std::ostringstream addressString;
        addressString << std::showbase << std::hex << m_i2cAddress;

        while (!m_exit) {
            if (i2cDevPresent("/dev/i2c-" + busString, m_i2cAddress)) {
                if (!std::filesystem::is_directory(m_hwmonDir)) {
                    spdlog::get("hardware")->info("Registering PSU at {}", addressString.str());
                    i2cLoadDriver(busString, addressString.str());
                }

                // The driver might already be loaded before the program starts. This ensures that the properties still
                // get initialized if that's the case.
                if (!m_hwmon) {
                    std::lock_guard lk(m_mtx);
                    createPower();
                }
            } else if (std::filesystem::is_directory(m_hwmonDir)) {
                {
                    std::lock_guard lk(m_mtx);
                    m_hwmon = nullptr;
                    m_properties.clear();
                }
                spdlog::get("hardware")->info("Deregistering PSU from {}", addressString.str());
                i2cUnloadDriver(busString, addressString.str());
            }

            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    });
}

FspYhPsu::~FspYhPsu()
{
    m_exit = true;
    m_psuWatcher.join();
}

void FspYhPsu::createPower()
{
    m_hwmon = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(m_hwmonDir);
    const auto prefix = "ne:"s + m_psuName;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;
    using velia::ietf_hardware::data_reader::Fans;
    using velia::ietf_hardware::data_reader::SensorType;
    m_properties.push_back(StaticData(prefix, "ne", {{"class", "iana-hardware:power-supply"}}));
    m_properties.push_back(SysfsValue<SensorType::Temperature>(prefix + ":temperature-1", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::Temperature>(prefix + ":temperature-2", prefix, m_hwmon, 2));
    m_properties.push_back(SysfsValue<SensorType::Current>(prefix + ":current-in", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::Current>(prefix + ":current-12V", prefix, m_hwmon, 2));
    m_properties.push_back(SysfsValue<SensorType::Current>(prefix + ":current-5Vsb", prefix, m_hwmon, 3));
    m_properties.push_back(SysfsValue<SensorType::VoltageAC>(prefix + ":voltage-in", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::VoltageDC>(prefix + ":voltage-12V", prefix, m_hwmon, 2));
    m_properties.push_back(SysfsValue<SensorType::VoltageDC>(prefix + ":voltage-5Vsb", prefix, m_hwmon, 3));
    m_properties.push_back(SysfsValue<SensorType::Power>(prefix + ":power-in", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::Power>(prefix + ":power-out", prefix, m_hwmon, 2));
    m_properties.push_back(Fans(prefix + ":fan", prefix, m_hwmon, 1));
}

DataTree FspYhPsu::readValues()
{
    std::map<std::string, std::string> res;

    std::lock_guard lk(m_mtx);
    for (auto& reader : m_properties) {
        res.merge(reader());
    }

    return res;
}
}
