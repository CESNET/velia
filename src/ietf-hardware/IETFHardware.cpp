/*
 * Copyright (C) 2016-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <boost/algorithm/hex.hpp>
#include <chrono>
#include <filesystem>
#include <libyang-cpp/Time.hpp>
#include <utility>
#include "IETFHardware.h"
#include "utils/log.h"
#include "utils/io.h"

using namespace std::literals;

namespace {

static const std::string ietfHardwareStatePrefix = "/ietf-hardware:hardware";

/** @brief Constructs a full XPath for a specific component */
std::string xpathForComponent(const std::string& componentName)
{
    return ietfHardwareStatePrefix + "/component[name='" + componentName + "']/";
}

/** @brief Prefix all properties from values DataTree with a component name (calculated from @p componentName) and push them into the DataTree */
void addComponent(velia::ietf_hardware::DataTree& res, const std::string& componentName, const std::optional<std::string>& parent, const velia::ietf_hardware::DataTree& values, const std::string& operState = "enabled")
{
    auto componentPrefix = xpathForComponent(componentName);

    if (parent) {
        res[componentPrefix + "parent"] = *parent;
    }
    for (const auto& [k, v] : values) {
        res[componentPrefix + k] = v;
    }

    res[componentPrefix + "state/oper-state"] = operState;
}

void writeSensorValue(velia::ietf_hardware::DataTree& res, const std::string& componentName, const std::string& value, const std::string& operStatus)
{
    const auto componentPrefix = xpathForComponent(componentName);
    res[componentPrefix + "sensor-data/value"] = value;
    res[componentPrefix + "sensor-data/oper-status"] = operStatus;
}

/** @brief Write a sensor-data @p value for a component @p componentName and push it into the @p res DataTree */
void addSensorValue(velia::Log log, velia::ietf_hardware::DataTree& res, const std::string& componentName, int64_t value)
{
    static constexpr const int64_t YANG_SENSOR_VALUE_MIN = -1'000'000'000;
    static constexpr const int64_t YANG_SENSOR_VALUE_MAX = 1'000'000'000;

    // FIXME: Valid value range depends on sensor-type as well, see ietf-hardware's sensor-value type description

    if (value <= YANG_SENSOR_VALUE_MIN) {
        log->error("Sensor's '{}' value '{}' underflow. Setting sensor as nonoperational.", componentName, value);
        writeSensorValue(res, componentName, std::to_string(YANG_SENSOR_VALUE_MIN), "nonoperational");
    } else if (value >= YANG_SENSOR_VALUE_MAX) {
        log->error("Sensor's '{}' value '{}' overflow. Setting sensor as nonoperational.", componentName, value);
        writeSensorValue(res, componentName, std::to_string(YANG_SENSOR_VALUE_MAX), "nonoperational");
    } else {
        writeSensorValue(res, componentName, std::to_string(value), "ok");
    }
}

/** @brief Write a sensor-data @p value for a component @p componentName and push it into the @p res DataTree */
void addSensorValue(velia::Log, velia::ietf_hardware::DataTree& res, const std::string& componentName, const std::string& value)
{
    // TODO: Perhaps we should check if the string value is conforming to sensor-value type
    writeSensorValue(res, componentName, value, "ok");
}
}

namespace velia::ietf_hardware {

void SensorPollData::merge(SensorPollData&& other)
{
    data.merge(other.data);
    thresholds.merge(other.thresholds);
    sideLoadedAlarms.merge(other.sideLoadedAlarms);
}

IETFHardware::IETFHardware()
    : m_log(spdlog::get("hardware"))
{
}

IETFHardware::~IETFHardware() = default;

/**
 * Calls individual registered data readers and processes information obtained from them.
 * The sensor values are then passed to threshold watchers to detect if the new value violated a threshold.
 * This function does not raise any alarms. It only returns the data tree *and* any changes in the threshold
 * crossings (as a mapping from sensor XPath to threshold watcher State (@see Watcher)).
 **/
HardwareInfo IETFHardware::process()
{
    SensorPollData pollData;
    std::set<std::string> activeSensors;
    std::map<std::string, ThresholdUpdate<int64_t>> alarms;

    for (auto& dataReader : m_callbacks) {
        pollData.merge(dataReader());
    }

    /* the thresholds watchers are created dynamically
     *  - when a new sensor occurs then we add a new watcher
     *  - when a sensor disappears we remove the corresponding watcher
     */
    for (const auto& [sensorXPath, sensorThresholds] : pollData.thresholds) {
        if (!m_thresholdsWatchers.contains(sensorXPath)) {
            m_thresholdsWatchers.emplace(sensorXPath, sensorThresholds);
        }
        activeSensors.emplace(sensorXPath);
    }

    for (auto& [sensorXPath, thresholdsWatcher] : m_thresholdsWatchers) {
        std::optional<int64_t> newValue;

        if (auto it = pollData.data.find(sensorXPath); it != pollData.data.end()) {
            newValue = std::stoll(it->second);
        } else {
            newValue = std::nullopt;
        }

        if (auto update = thresholdsWatcher.update(newValue)) {
            m_log->debug("threshold: {} {}", sensorXPath, update->newState);
            alarms.emplace(sensorXPath, *update);
        }
    }

    pollData.data[ietfHardwareStatePrefix + "/last-change"] = libyang::yangTimeFormat(std::chrono::system_clock::now(), libyang::TimezoneInterpretation::Unspecified);

    return {pollData.data, alarms, activeSensors, pollData.sideLoadedAlarms};
}

void IETFHardware::registerDataReader(const IETFHardware::DataReader& callable)
{
    m_callbacks.push_back(callable);
}

/** @brief A namespace containing predefined data readers for IETFHardware class.
 * @see IETFHardware for more information
 */
namespace data_reader {

DataReader::DataReader(std::string componentName, std::optional<std::string> parent)
    : m_componentName(std::move(componentName))
    , m_parent(std::move(parent))
    , m_log(spdlog::get("hardware"))
{
}

/** @brief Constructs a component without any sensor-data and provide only the static data @p dataTree passed via constructor.
 * @param componentName the name of the component in the resulting tree
 * @param parent The component in YANG model has a link to parent. Specify who is the parent.
 * @param dataTree static data to insert into the resulting tree. The dataTree keys should only contain the YANG node name, not full XPath. The full XPath is constructed from @componentName and the map key.
 */
StaticData::StaticData(std::string componentName, std::optional<std::string> parent, DataTree dataTree)
    : DataReader(std::move(componentName), std::move(parent))
{
    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 dataTree);
}

SensorPollData StaticData::operator()() const { return {m_staticData, {}, {}}; }

Fans::Fans(std::string componentName, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fanChannelsCount, Thresholds<int64_t> thresholds)
    : DataReader(std::move(componentName), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_fanChannelsCount(fanChannelsCount)
    , m_thresholds(std::move(thresholds))
{
    // fans
    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 DataTree{
                     {"class", "iana-hardware:module"}, // FIXME: Read (or pass via constructor) additional properties (mfg, model, ...). They should be in the fans' tray EEPROM.
                 });

    for (unsigned i = 1; i <= m_fanChannelsCount; i++) {
        // fans -> fan_i
        addComponent(m_staticData,
                     m_componentName + ":fan" + std::to_string(i),
                     m_componentName,
                     DataTree{
                         {"class", "iana-hardware:fan"},
                     });

        // fans -> fan_i -> sensor-data
        addComponent(m_staticData,
                     m_componentName + ":fan" + std::to_string(i) + ":rpm",
                     m_componentName + ":fan" + std::to_string(i),
                     DataTree{
                         {"class", "iana-hardware:sensor"},
                         {"sensor-data/value-type", "rpm"},
                         {"sensor-data/value-scale", "units"},
                         {"sensor-data/value-precision", "0"},
                     });
    }
}

SensorPollData Fans::operator()() const
{
    DataTree data(m_staticData);
    for (unsigned i = 1; i <= m_fanChannelsCount; i++) {
        const auto sensorComponentName = m_componentName + ":fan" + std::to_string(i) + ":rpm";
        const auto attribute = "fan"s + std::to_string(i) + "_input";

        addSensorValue(m_log, data, sensorComponentName, m_hwmon->attribute(attribute));
    }

    ThresholdsBySensorPath thr;
    for (unsigned i = 1; i <= m_fanChannelsCount; i++) {
        thr.emplace(xpathForComponent(m_componentName + ":fan" + std::to_string(i) + ":rpm") + "sensor-data/value", m_thresholds);
    }

    return {data, thr, {}};
}

CzechLightFans::CzechLightFans(std::string componentName,
                               std::optional<std::string> parent,
                               std::shared_ptr<sysfs::HWMon> hwmon,
                               unsigned fanChannelsCount,
                               Thresholds<int64_t> thresholds,
                               const SerialNumberCallback& cbSerialNumber)
    : Fans(std::move(componentName), std::move(parent), std::move(hwmon), std::move(fanChannelsCount), std::move(thresholds))
    , m_serialNumber(std::move(cbSerialNumber))
{
}

SensorPollData CzechLightFans::operator()() const
{
    auto res = Fans::operator()();

    if (auto eeprom = m_serialNumber()) {
        // if the EEPROM is readable, then we assume that the component "is there"
        res.data[xpathForComponent(m_componentName) + "state/oper-state"] = "enabled";
        res.data[xpathForComponent(m_componentName) + "serial-num"] = *eeprom;
    } else {
        // EEPROM expected, but not readable -> mark as fubar
        res.data[xpathForComponent(m_componentName) + "state/oper-state"] = "disabled";
    }

    // FIXME: do "something" when the S/N from EEPROM has changed. That's our only way of detecting
    // board un/re/plugging, so there should be an alarm if stuff is not plugged in, and a notification
    // when it gets changed. Fortunately we still process performance data about fan speeds, so the
    // user "will know" if there's a problem.

    return res;
}

std::string getSysfsFilename(const SensorType type, int sysfsChannelNr)
{
    switch (type) {
    case SensorType::Temperature:
        return "temp"s + std::to_string(sysfsChannelNr) + "_input";
    case SensorType::Current:
        return "curr"s + std::to_string(sysfsChannelNr) + "_input";
    case SensorType::Power:
        return "power"s + std::to_string(sysfsChannelNr) + "_input";
    case SensorType::VoltageAC:
    case SensorType::VoltageDC:
        return "in"s + std::to_string(sysfsChannelNr) + "_input";
    }

    __builtin_unreachable();
}

template <SensorType TYPE>
const DataTree sysfsStaticData;
template <>
const DataTree sysfsStaticData<SensorType::Temperature> = {
    {"class", "iana-hardware:sensor"},
    {"sensor-data/value-type", "celsius"},
    {"sensor-data/value-scale", "milli"},
    {"sensor-data/value-precision", "0"},
};
template <>
const DataTree sysfsStaticData<SensorType::Current> = {
    {"class", "iana-hardware:sensor"},
    {"sensor-data/value-type", "amperes"},
    {"sensor-data/value-scale", "milli"},
    {"sensor-data/value-precision", "0"},
};
template <>
const DataTree sysfsStaticData<SensorType::Power> = {
    {"class", "iana-hardware:sensor"},
    {"sensor-data/value-type", "watts"},
    {"sensor-data/value-scale", "micro"},
    {"sensor-data/value-precision", "0"},
};
template <>
const DataTree sysfsStaticData<SensorType::VoltageAC> = {
    {"class", "iana-hardware:sensor"},
    {"sensor-data/value-type", "volts-AC"},
    {"sensor-data/value-scale", "milli"},
    {"sensor-data/value-precision", "0"},
};
template <>
const DataTree sysfsStaticData<SensorType::VoltageDC> = {
    {"class", "iana-hardware:sensor"},
    {"sensor-data/value-type", "volts-DC"},
    {"sensor-data/value-scale", "milli"},
    {"sensor-data/value-precision", "0"},
};

template <SensorType TYPE>
SysfsValue<TYPE>::SysfsValue(std::string componentName, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr, Thresholds<int64_t> thresholds)
    : DataReader(std::move(componentName), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_sysfsFile(getSysfsFilename(TYPE, sysfsChannelNr))
    , m_thresholds(std::move(thresholds))
{
    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 sysfsStaticData<TYPE>);
}

template <SensorType TYPE>
SensorPollData SysfsValue<TYPE>::operator()() const
{
    DataTree res(m_staticData);

    int64_t sensorValue = m_hwmon->attribute(m_sysfsFile);
    addSensorValue(m_log, res, m_componentName, sensorValue);

    return {res, ThresholdsBySensorPath{{xpathForComponent(m_componentName) + "sensor-data/value", m_thresholds}}, {}};
}

template struct SysfsValue<SensorType::Current>;
template struct SysfsValue<SensorType::Power>;
template struct SysfsValue<SensorType::Temperature>;
template struct SysfsValue<SensorType::VoltageAC>;
template struct SysfsValue<SensorType::VoltageDC>;

EMMC::EMMC(std::string componentName, std::optional<std::string> parent, std::shared_ptr<sysfs::EMMC> emmc, Thresholds<int64_t> thresholds)
    : DataReader(std::move(componentName), std::move(parent))
    , m_emmc(std::move(emmc))
    , m_thresholds(std::move(thresholds))
{
    auto emmcAttrs = m_emmc->attributes();

    // date is specified in MM/YYYY format (source: kernel core/mmc.c) and mfg-date is unfortunately of type yang:date-and-time
    std::string mfgDate = emmcAttrs.at("date");
    std::chrono::year_month_day calendarDate(
        std::chrono::year(std::stoi(mfgDate.substr(3, 4))),
        std::chrono::month(std::stoi(mfgDate.substr(0, 2))),
        std::chrono::day(1));
    mfgDate = libyang::yangTimeFormat(std::chrono::sys_days{calendarDate}, libyang::TimezoneInterpretation::Unspecified);

    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 DataTree{
                     {"class", "iana-hardware:module"},
                     {"mfg-date", mfgDate},
                     {"serial-num", emmcAttrs.at("serial")},
                     {"model-name", emmcAttrs.at("name")},
                 });

    addComponent(m_staticData,
                 m_componentName + ":lifetime",
                 m_componentName,
                 DataTree{
                     {"class", "iana-hardware:sensor"},
                     {"sensor-data/value-type", "other"},
                     {"sensor-data/value-scale", "units"},
                     {"sensor-data/value-precision", "0"},
                     {"sensor-data/units-display", "percent"s},
                 });
}

SensorPollData EMMC::operator()() const
{
    DataTree data(m_staticData);

    auto emmcAttrs = m_emmc->attributes();
    addSensorValue(m_log, data, m_componentName + ":lifetime", emmcAttrs.at("life_time"));

    return {data, ThresholdsBySensorPath{{xpathForComponent(m_componentName + ":lifetime") + "sensor-data/value", m_thresholds}}, {}};
}

EepromWithUid::EepromWithUid(std::string componentName, std::optional<std::string> parent, const std::string& sysfsPrefix, const uint8_t bus, const uint8_t address, const uint32_t totalSize, const uint32_t offset, const uint32_t length)
    : DataReader(std::move(componentName), std::move(parent))
{
    DataTree tree{
        {"class", "iana-hardware:module"},
    };

    if (auto sn = hexEEPROM(sysfsPrefix, bus, address, totalSize, offset, length)) {
        tree["serial-num"] = *sn;
    }

    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 tree,
                 tree.count("serial-num") ? "enabled" : "disabled");
}

SensorPollData EepromWithUid::operator()() const { return {m_staticData, {}, {}}; }
}

std::optional<std::string> hexEEPROM(const std::string& sysfsPrefix,
                                     const uint8_t bus,
                                     const uint8_t address,
                                     const uint32_t totalSize,
                                     const uint32_t offset,
                                     const uint32_t length)
{
    namespace fs = std::filesystem;
    auto log = spdlog::get("hardware");

    if (offset + length > totalSize) {
        throw std::logic_error{"EEPROM: region out of range"};
    }

    auto dirname = fs::path{fmt::format("{}/bus/i2c/devices/{}-{:04x}", sysfsPrefix, bus, address)};
    if (!fs::is_directory(dirname)) {
        // this is a hard error because the device is expected to always exist, even when it fails to probe
        throw std::runtime_error{fmt::format("EEPROM: no I2C device defined at bus {} address 0x{:02x}", bus, address)};
    }
    auto filename = dirname / "eeprom";
    try {
        // any errors are "soft errors": older clearfog boards don't have these EEPROMs populated at all
        if (!fs::is_regular_file(filename)) {
            throw std::runtime_error{"sysfs entry missing"};
        }
        auto buf = velia::utils::readFileToBytes(filename);
        if (buf.size() != totalSize) {
            throw std::runtime_error{fmt::format("expected {} bytes of data, got {}", totalSize, buf.size())};
        }
        std::string res;
        res.reserve(length * 2 /* two hex characters per byte */);
        boost::algorithm::hex(buf.begin() + offset, buf.begin() + offset + length, std::back_inserter(res));
        log->trace("I2C EEPROM at bus {} address {:#04x}: UID/EUI {}", bus, address, res);
        return res;
    } catch (const std::exception& e) {
        log->error("EEPROM: cannot read from {}: {}", filename.string(), e.what());
    }
    return std::nullopt;
}
}
