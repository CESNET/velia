#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "FspYh.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/thresholds.h"
#include "utils/UniqueResource.h"
#include "utils/log.h"

namespace {
const auto ALARM_SENSOR_MISSING = "velia-alarms:sensor-missing-alarm";
const auto ALARM_SENSOR_MISSING_SEVERITY = "critical";
}

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
    spdlog::get("hardware")->info("Registering {} at {}", m_driverName, m_addressString);
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
    spdlog::get("hardware")->info("Deregistering {} from {}", m_driverName, m_addressString);
    std::ofstream ofs(m_unbindPath);
    if (!ofs.is_open()) {
        throw std::runtime_error("TransientI2C::unbind(): can't open file '" + m_unbindPath + "'");
    }
    ofs << m_addressString;
    if (ofs.bad()) {
        throw std::runtime_error("TransientI2C::unbind(): can't write file '" + m_unbindPath + "'");
    }
}

FspYh::FspYh(const std::filesystem::path& hwmonDir, const std::string& name, std::shared_ptr<TransientI2C> i2c)
    : m_i2c(i2c)
    , m_hwmonDir(hwmonDir)
    , m_namePrefix("ne:"s + name)
    , m_staticData(velia::ietf_hardware::data_reader::StaticData(m_namePrefix, "ne", {{"class", "iana-hardware:power-supply"}})().data)
{
    m_exit = false;
}

void FspYh::startThread() {
    // Always run at least once to prevent a false positive initial "there's no device here"
    pollDevicePresence();

    m_psuWatcher = std::jthread([this] {
        while (!m_exit) {
            pollDevicePresence();
            std::unique_lock lock(m_mtx);
            m_cond.wait_for(lock, std::chrono::seconds(3));
        }
    });
}

void FspYh::pollDevicePresence()
{
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
}

FspYh::~FspYh()
{
    m_exit = true;
}

SensorPollData FspYh::readValues()
{
    auto componentXPath = "/ietf-hardware:hardware/component[name='"s + m_namePrefix + "']";

    std::unique_lock lock(m_mtx);

    SensorPollData res;
    res.data = m_staticData;

    if (m_properties.empty()) {
        res.data[componentXPath + "/state/oper-state"] = "disabled";
        res.sideLoadedAlarms.insert({ALARM_SENSOR_MISSING, componentXPath, ALARM_SENSOR_MISSING_SEVERITY, missingAlarmDescription()});
        return res;
    }

    for (auto& reader : m_properties) {
        try {
            res.merge(reader());
        } catch (std::logic_error& ex) {
            // The PSU or PDU might get disconnected before the watcher thread is able to react. Because of this, the sysfs
            // read can fail. We must react to this and catch the exception from readFileInt64. If we cannot get all
            // data, we'll consider the data we got as invalid, so we'll return an empty map.
            spdlog::get("hardware")->warn("Couldn't read {} sysfs data (maybe the device was just ejected?): {}", m_namePrefix, ex.what());

            res.data = m_staticData;
            res.data["/ietf-hardware:hardware/component[name='" + m_namePrefix + "']/state/oper-state"] = "disabled";
            res.thresholds.clear();
            res.sideLoadedAlarms.insert({ALARM_SENSOR_MISSING, componentXPath, ALARM_SENSOR_MISSING_SEVERITY, missingAlarmDescription()});

            lock.unlock();
            m_cond.notify_all();

            return res;
        }
    }

    res.sideLoadedAlarms.insert({ALARM_SENSOR_MISSING, componentXPath, "cleared", missingAlarmDescription()});
    return res;
}

FspYhPsu::FspYhPsu(const std::filesystem::path& hwmonDir, const std::string& psu, std::shared_ptr<TransientI2C> i2c)
    : FspYh(hwmonDir, psu, i2c)
{
    startThread();
}

void FspYhPsu::createPower()
{
    m_hwmon = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(m_hwmonDir);
    using velia::ietf_hardware::OneThreshold;
    using velia::ietf_hardware::Thresholds;
    using velia::ietf_hardware::data_reader::Fans;
    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::SysfsValue;

    auto registerReader = [&]<typename DataReaderType>(DataReaderType&& reader) {
        m_properties.emplace_back(reader);
    };

    registerReader(SysfsValue<SensorType::Temperature>(m_namePrefix + ":temperature-1",
                                                       m_namePrefix,
                                                       m_hwmon,
                                                       1,
                                                       Thresholds<int64_t>{
                                                           .criticalLow = std::nullopt,
                                                           .warningLow = std::nullopt,
                                                           .warningHigh = OneThreshold<int64_t>{40000, 1000},
                                                           .criticalHigh = OneThreshold<int64_t>{45000, 1000},
                                                       }));
    registerReader(SysfsValue<SensorType::Temperature>(m_namePrefix + ":temperature-2",
                                                       m_namePrefix,
                                                       m_hwmon,
                                                       2,
                                                       Thresholds<int64_t>{
                                                           .criticalLow = std::nullopt,
                                                           .warningLow = std::nullopt,
                                                           .warningHigh = OneThreshold<int64_t>{40000, 1000},
                                                           .criticalHigh = OneThreshold<int64_t>{45000, 1000},
                                                       }));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-in", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-12V", m_namePrefix, m_hwmon, 2));
    registerReader(SysfsValue<SensorType::VoltageAC>(m_namePrefix + ":voltage-in",
                                                     m_namePrefix,
                                                     m_hwmon,
                                                     1,
                                                     Thresholds<int64_t>{
                                                         .criticalLow = OneThreshold<int64_t>{90000, 3000},
                                                         .warningLow = OneThreshold<int64_t>{100000, 3000},
                                                         .warningHigh = OneThreshold<int64_t>{245000, 3000},
                                                         .criticalHigh = OneThreshold<int64_t>{264000, 3000},
                                                     }));
    registerReader(SysfsValue<SensorType::VoltageDC>(m_namePrefix + ":voltage-12V",
                                                     m_namePrefix,
                                                     m_hwmon,
                                                     2,
                                                     Thresholds<int64_t>{
                                                         .criticalLow = OneThreshold<int64_t>{11300, 50},
                                                         .warningLow = OneThreshold<int64_t>{11500, 50},
                                                         .warningHigh = OneThreshold<int64_t>{12500, 50},
                                                         .criticalHigh = OneThreshold<int64_t>{12700, 50},
                                                     }));
    registerReader(SysfsValue<SensorType::Power>(m_namePrefix + ":power-in", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Power>(m_namePrefix + ":power-out", m_namePrefix, m_hwmon, 2));
    registerReader(Fans(m_namePrefix + ":fan",
                        m_namePrefix,
                        m_hwmon,
                        1,
                        Thresholds<int64_t>{
                            .criticalLow = OneThreshold<int64_t>{1500, 150}, // datasheet YH5151 (sec. 3.4) says critical is 1000 and warning 2000; giving 500rpm extra reserve
                            .warningLow = OneThreshold<int64_t>{2500, 150},
                            .warningHigh = std::nullopt,
                            .criticalHigh = std::nullopt,
                        }));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-5Vsb", m_namePrefix, m_hwmon, 3));
    registerReader(SysfsValue<SensorType::VoltageDC>(m_namePrefix + ":voltage-5Vsb",
                                                     m_namePrefix,
                                                     m_hwmon,
                                                     3,
                                                     Thresholds<int64_t>{
                                                         .criticalLow = OneThreshold<int64_t>{4600, 50},
                                                         .warningLow = OneThreshold<int64_t>{4700, 50},
                                                         .warningHigh = OneThreshold<int64_t>{5300, 50},
                                                         .criticalHigh = OneThreshold<int64_t>{5400, 50},
                                                     }));
}

std::string FspYhPsu::missingAlarmDescription() const
{
    return "PSU is unplugged.";
}

FspYhPdu::FspYhPdu(const std::filesystem::path& hwmonDir, const std::string& pdu, std::shared_ptr<TransientI2C> i2c)
    : FspYh(hwmonDir, pdu, i2c)
{
    startThread();
}

void FspYhPdu::createPower()
{
    m_hwmon = std::make_shared<velia::ietf_hardware::sysfs::HWMon>(m_hwmonDir);

    using velia::ietf_hardware::OneThreshold;
    using velia::ietf_hardware::Thresholds;
    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::SysfsValue;

    auto registerReader = [&]<typename DataReaderType>(DataReaderType&& reader) {
        m_properties.emplace_back(reader);
    };

    /*
     * The order of reading hwmon files of the PDU is important.
     * Reading properties from hwmon can trigger page change in the device which can take more than 20ms.
     * We have therefore grouped the properties based on their page location to minimize the page changes.
     * See linux/drivers/hwmon/pmbus/fsp-3y.c
     */

    registerReader(SysfsValue<SensorType::VoltageDC>(m_namePrefix + ":voltage-12V",
                                                     m_namePrefix,
                                                     m_hwmon,
                                                     1,
                                                     Thresholds<int64_t>{
                                                         .criticalLow = OneThreshold<int64_t>{11300, 50},
                                                         .warningLow = OneThreshold<int64_t>{11500, 50},
                                                         .warningHigh = OneThreshold<int64_t>{12500, 50},
                                                         .criticalHigh = OneThreshold<int64_t>{12700, 50},
                                                     }));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-12V", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Power>(m_namePrefix + ":power-12V", m_namePrefix, m_hwmon, 1));
    registerReader(SysfsValue<SensorType::Temperature>(m_namePrefix + ":temperature-1",
                                                       m_namePrefix,
                                                       m_hwmon,
                                                       1,
                                                       Thresholds<int64_t>{
                                                           .criticalLow = std::nullopt,
                                                           .warningLow = std::nullopt,
                                                           .warningHigh = OneThreshold<int64_t>{40000, 1000},
                                                           .criticalHigh = OneThreshold<int64_t>{45000, 1000},
                                                       }));
    registerReader(SysfsValue<SensorType::Temperature>(m_namePrefix + ":temperature-2",
                                                       m_namePrefix,
                                                       m_hwmon,
                                                       2,
                                                       Thresholds<int64_t>{
                                                           .criticalLow = std::nullopt,
                                                           .warningLow = std::nullopt,
                                                           .warningHigh = OneThreshold<int64_t>{40000, 1000},
                                                           .criticalHigh = OneThreshold<int64_t>{45000, 1000},
                                                       }));
    registerReader(SysfsValue<SensorType::Temperature>(m_namePrefix + ":temperature-3",
                                                       m_namePrefix,
                                                       m_hwmon,
                                                       3,
                                                       Thresholds<int64_t>{
                                                           .criticalLow = std::nullopt,
                                                           .warningLow = std::nullopt,
                                                           .warningHigh = OneThreshold<int64_t>{40000, 1000},
                                                           .criticalHigh = OneThreshold<int64_t>{45000, 1000},
                                                       }));

    registerReader(SysfsValue<SensorType::VoltageDC>(m_namePrefix + ":voltage-5V",
                                                     m_namePrefix,
                                                     m_hwmon,
                                                     2,
                                                     Thresholds<int64_t>{
                                                         .criticalLow = OneThreshold<int64_t>{4600, 50},
                                                         .warningLow = OneThreshold<int64_t>{4700, 50},
                                                         .warningHigh = OneThreshold<int64_t>{5300, 50},
                                                         .criticalHigh = OneThreshold<int64_t>{5400, 50},
                                                     }));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-5V", m_namePrefix, m_hwmon, 2));
    registerReader(SysfsValue<SensorType::Power>(m_namePrefix + ":power-5V", m_namePrefix, m_hwmon, 2));

    registerReader(SysfsValue<SensorType::VoltageDC>(m_namePrefix + ":voltage-3V3",
                                                     m_namePrefix,
                                                     m_hwmon,
                                                     3,
                                                     Thresholds<int64_t>{
                                                         .criticalLow = OneThreshold<int64_t>{3100, 25},
                                                         .warningLow = OneThreshold<int64_t>{3200, 25},
                                                         .warningHigh = OneThreshold<int64_t>{3400, 25},
                                                         .criticalHigh = OneThreshold<int64_t>{3500, 25},
                                                     }));
    registerReader(SysfsValue<SensorType::Current>(m_namePrefix + ":current-3V3", m_namePrefix, m_hwmon, 3));
    registerReader(SysfsValue<SensorType::Power>(m_namePrefix + ":power-3V3", m_namePrefix, m_hwmon, 3));
}

std::string FspYhPdu::missingAlarmDescription() const
{
    return "I2C read failure for PDU. Could not get hardware sensor details.";
}
}
