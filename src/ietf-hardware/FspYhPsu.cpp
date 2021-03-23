#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "FspYhPsu.h"
#include "utils/log.h"
#include "utils/UniqueResource.h"

namespace velia::ietf_hardware {

TransientI2C::TransientI2C(const uint8_t bus, const uint8_t address, const std::string& driverName)
    : m_address(address)
    , m_driverName(driverName)
    , m_isPresentPath("/dev/i2c-" + std::to_string(bus))
    , m_bindPath("/sys/bus/i2c/devices/i2c-" + std::to_string(bus) + "/new_device")
    , m_unbindPath("/sys/bus/i2c/devices/i2c-" + std::to_string(bus) + "/delete_device")
{
    std::ostringstream addressString;
    addressString << std::showbase << std::hex << int{m_address};
    m_addressString = addressString.str();
}

TransientI2C::~TransientI2C() = default;

bool TransientI2C::isPresent() const
{
    auto file = open(m_isPresentPath.c_str(), O_RDWR);
    if (file < 0) {
        throw std::runtime_error("TransientI2C::isPresent(): "s + strerror(errno));
    }

    auto fdClose = make_unique_resource([] {}, [file] {
        close(file);
    });

    if (ioctl(file, I2C_SLAVE_FORCE, m_address) < 0) {
        throw std::runtime_error("TransientI2C::isPresent(): "s + strerror(errno));
    }

    char bufferIn[1];
    return read(file, bufferIn, 1) != -1;

}

void TransientI2C::bind() const
{
    spdlog::get("hardware")->info("Registering PSU at {}", m_addressString);
    std::ofstream ofs(m_bindPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("TransientI2C::bind(): can't open file '" + m_bindPath + "'");
    }
    ofs << m_driverName << " " << m_addressString;
    if (ofs.bad()) {
        throw std::runtime_error("TransientI2C::bind(): can't write file '" + m_bindPath + "'");
    }
}
void TransientI2C::unbind() const
{
    spdlog::get("hardware")->info("Deregistering PSU from {}", m_addressString);
    std::ofstream ofs(m_unbindPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("TransientI2C::unbind(): can't open file '" + m_unbindPath + "'");
    }
    ofs << m_addressString;
    if (ofs.bad()) {
        throw std::runtime_error("TransientI2C::unbind(): can't write file '" + m_unbindPath + "'");
    }
}

FspYhPsu::FspYhPsu(const std::filesystem::path& hwmonDir, const std::string& psuName, std::shared_ptr<TransientI2C> i2c)
    : m_i2c(i2c)
    , m_hwmonDir(hwmonDir)
    , m_psuName(psuName)
{
    m_exit = false;
    m_psuWatcher = std::thread([this] {

        while (!m_exit) {
            if (m_i2c->isPresent()) {
                if (!std::filesystem::is_directory(m_hwmonDir)) {
                    m_i2c->bind();
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

                m_i2c->unbind();
            }

            std::unique_lock lock(m_mtx);
            m_cond.wait_for(lock, std::chrono::seconds(3));
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

    std::unique_lock lock(m_mtx);
    for (auto& reader : m_properties) {
        try {
            res.merge(reader());
        } catch (std::logic_error&) {
            // The PSU might get disconnected before the watcher thread is able to react. Because of this, the sysfs
            // read can fail. We must react to this and catch the exception from readFileInt64. If we cannot get all
            // data, we'll consider the data we got as invalid, so we'll return an empty map.
            spdlog::get("hardware")->warn("Couldn't read PSU sysfs data (maybe the PSU was just ejected?)");
            lock.unlock();
            m_cond.notify_all();
            return {};
        }

    }

    return res;
}
}
