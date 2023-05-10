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
        throw std::system_error(errno, std::system_category(), "TransientI2C::isPresent: open()");
    }

    auto fdClose = utils::make_unique_resource([] {}, [file] {
        close(file);
    });

    if (ioctl(file, I2C_SLAVE_FORCE, m_address) < 0) {
        throw std::system_error(errno, std::system_category(), "TransientI2C::isPresent: ioctl()");
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
    , m_namePrefix("ne:"s + psuName)
    , m_staticData(velia::ietf_hardware::data_reader::StaticData(m_namePrefix, "ne", {{"class", "iana-hardware:power-supply"}})())
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

            // There is a bug, where TSan falsely reports "double lock of a mutex", one here, in the watcher thread and
            // another, in readValues(). These locks are in a different thread. The report can be fixed by using
            // different mutexes for this condition variable and for the reading/binding/unbinding. Work around this by
            // using different mutexes (instead of creating a suppression).
            // FIXME: This false positive is fixed in LLVM 12, so remove this after it's available.
            // // https://github.com/google/sanitizers/issues/1259
            std::unique_lock lock(m_condMtx);
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
    using velia::ietf_hardware::data_reader::SysfsValue;
    using velia::ietf_hardware::data_reader::Fans;
    using velia::ietf_hardware::data_reader::SensorType;


    auto registerReader = [&]<typename DataReaderType>(DataReaderType&& reader) {
        m_thresholds.merge(reader.thresholds());
        m_properties.emplace_back(reader);
    };

    registerReader(SysfsValue<SensorType::Temperature>(m_namePrefix + ":temperature-1", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Temperature>(m_namePrefix + ":temperature-2", m_namePrefix, m_hwmon, 2));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-in", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-12V", m_namePrefix, m_hwmon, 2));
    registerReader(SysfsValue<SensorType::VoltageAC>(m_namePrefix + ":voltage-in", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::VoltageDC>(m_namePrefix + ":voltage-12V", m_namePrefix, m_hwmon, 2));
    registerReader(SysfsValue<SensorType::Power>(m_namePrefix + ":power-in", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Power>(m_namePrefix + ":power-out", m_namePrefix, m_hwmon, 2));
    registerReader(Fans(m_namePrefix + ":fan", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-5Vsb", m_namePrefix, m_hwmon, 3));
    registerReader(SysfsValue<SensorType::VoltageDC>(m_namePrefix + ":voltage-5Vsb", m_namePrefix, m_hwmon, 3));
}

DataTree FspYhPsu::readValues()
{
    std::unique_lock lock(m_mtx);

    DataTree res(m_staticData);

    if (m_properties.empty()) {
        res["/ietf-hardware:hardware/component[name='" + m_namePrefix + "']/state/oper-state"] = "disabled";
        return res;
    }

    for (auto& reader : m_properties) {
        try {
            res.merge(reader());
        } catch (std::logic_error& ex) {
            // The PSU might get disconnected before the watcher thread is able to react. Because of this, the sysfs
            // read can fail. We must react to this and catch the exception from readFileInt64. If we cannot get all
            // data, we'll consider the data we got as invalid, so we'll return an empty map.
            spdlog::get("hardware")->warn("Couldn't read PSU sysfs data (maybe the PSU was just ejected?): "s + ex.what());

            res = m_staticData;
            res["/ietf-hardware:hardware/component[name='" + m_namePrefix + "']/state/oper-state"] = "disabled";

            lock.unlock();
            m_cond.notify_all();

            return res;
        }

    }

    return res;
}

ThresholdsBySensorPath FspYhPsu::thresholds() const
{
    return m_thresholds;
}
}
