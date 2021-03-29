/*
 * Copyright (C) 2016-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <utility>
#include "IETFHardware.h"
#include "utils/log.h"
#include "utils/time.h"

using namespace std::literals;

namespace {

static const std::string ietfHardwareStatePrefix = "/ietf-hardware:hardware";

/** @brief Constructs a full XPath for a specific component */
std::string xpathForComponent(const std::string& componentName)
{
    return ietfHardwareStatePrefix + "/component[name='" + componentName + "']/";
}

/** @brief Prefix all properties from values DataTree with a component name (calculated from @p componentName) and push them into the DataTree */
void addComponent(velia::ietf_hardware::DataTree& res, const std::string& componentName, const std::optional<std::string>& parent, const velia::ietf_hardware::DataTree& values)
{
    auto componentPrefix = xpathForComponent(componentName);

    if (parent) {
        res[componentPrefix + "parent"] = *parent;
    }
    for (const auto& [k, v] : values) {
        res[componentPrefix + k] = v;
    }
}

/** @brief Write a sensor-data @p value for a component @p componentName and push it into the @p res DataTree */
void addSensorValue(velia::ietf_hardware::DataTree& res, const std::string& componentName, const std::string& value)
{
    const auto componentPrefix = xpathForComponent(componentName);
    res[componentPrefix + "sensor-data/value"] = value;
}
}

namespace velia::ietf_hardware {

IETFHardware::IETFHardware() = default;

IETFHardware::~IETFHardware() = default;

std::map<std::string, std::string> IETFHardware::process()
{
    std::map<std::string, std::string> res;

    for (auto& dataReader : m_callbacks) {
        res.merge(dataReader());
    }

    res[ietfHardwareStatePrefix + "/last-change"] = velia::utils::yangTimeFormat(std::chrono::system_clock::now());
    return res;
}

void IETFHardware::registerDataReader(const DataReader& callable)
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

DataTree StaticData::operator()() const { return m_staticData; }

Fans::Fans(std::string componentName, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fanChannelsCount)
    : DataReader(std::move(componentName), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_fanChannelsCount(fanChannelsCount)
{
    // fans
    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 DataTree {
                     {"class", "iana-hardware:module"}, // FIXME: Read (or pass via constructor) additional properties (mfg, model, ...). They should be in the fans' tray EEPROM.
                 });

    for (unsigned i = 1; i <= m_fanChannelsCount; i++) {
        // fans -> fan_i
        addComponent(m_staticData,
                     m_componentName + ":fan" + std::to_string(i),
                     m_componentName,
                     DataTree {
                         {"class", "iana-hardware:fan"},
                     });

        // fans -> fan_i -> sensor-data
        addComponent(m_staticData,
                     m_componentName + ":fan" + std::to_string(i) + ":rpm",
                     m_componentName + ":fan" + std::to_string(i),
                     DataTree {
                         {"class", "iana-hardware:sensor"},
                         {"sensor-data/value-type", "rpm"},
                         {"sensor-data/value-scale", "units"},
                         {"sensor-data/value-precision", "0"},
                         {"sensor-data/oper-status", "ok"},
                     });
    }
}

DataTree Fans::operator()() const
{
    DataTree res(m_staticData);

    auto attrs = m_hwmon->attributes();

    for (unsigned i = 1; i <= m_fanChannelsCount; i++) {
        const auto sensorComponentName = m_componentName + ":fan" + std::to_string(i) + ":rpm";
        const auto attribute = "fan"s + std::to_string(i) + "_input";

        addSensorValue(res, sensorComponentName, std::to_string(attrs.at(attribute)));
    }

    return res;
}

std::string getSysfsFilename(const SensorType type, int sysfsChannelNr)
{
    switch (type) {
        case SensorType::Temperature:
            return "temp"s + std::to_string(sysfsChannelNr) + "_input";
    }

    __builtin_unreachable();
}

template <SensorType TYPE> const DataTree sysfsStaticData;
template <> const DataTree sysfsStaticData<SensorType::Temperature> = {
    {"class", "iana-hardware:sensor"},
    {"sensor-data/value-type", "celsius"},
    {"sensor-data/value-scale", "milli"},
    {"sensor-data/value-precision", "0"},
    {"sensor-data/oper-status", "ok"},
};

template <SensorType TYPE>
SysfsValue<TYPE>::SysfsValue(std::string componentName, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr)
    : DataReader(std::move(componentName), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_sysfsFile(getSysfsFilename(TYPE, sysfsChannelNr))
{
    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 sysfsStaticData<TYPE>);
}

template <SensorType TYPE>
DataTree SysfsValue<TYPE>::operator()() const
{
    DataTree res(m_staticData);

    int64_t sensorValue = m_hwmon->attributes().at(m_sysfsFile);
    addSensorValue(res, m_componentName, std::to_string(sensorValue));

    return res;
}

template struct SysfsValue<SensorType::Temperature>;

EMMC::EMMC(std::string componentName, std::optional<std::string> parent, std::shared_ptr<sysfs::EMMC> emmc)
    : DataReader(std::move(componentName), std::move(parent))
    , m_emmc(std::move(emmc))
{
    auto emmcAttrs = m_emmc->attributes();

    // date is specified in MM/YYYY format (source: kernel core/mmc.c) and mfg-date is unfortunately of type yang:date-and-time
    std::string mfgDate = emmcAttrs.at("date");
    mfgDate = mfgDate.substr(3, 4) + "-" + mfgDate.substr(0, 2) + "-01T00:00:00Z";

    addComponent(m_staticData,
                 m_componentName,
                 m_parent,
                 DataTree {
                     {"class", "iana-hardware:module"},
                     {"mfg-date", mfgDate},
                     {"serial-num", emmcAttrs.at("serial")},
                     {"model-name", emmcAttrs.at("name")},
                 });

    addComponent(m_staticData,
                 m_componentName + ":lifetime",
                 m_componentName,
                 DataTree {
                     {"class", "iana-hardware:sensor"},
                     {"sensor-data/value-type", "other"},
                     {"sensor-data/value-scale", "units"},
                     {"sensor-data/value-precision", "0"},
                     {"sensor-data/oper-status", "ok"},
                     {"sensor-data/units-display", "percent"s},
                 });
}

DataTree EMMC::operator()() const
{
    DataTree res(m_staticData);

    auto emmcAttrs = m_emmc->attributes();
    addSensorValue(res, m_componentName + ":lifetime", emmcAttrs.at("life_time"));

    return res;
}
}
}
