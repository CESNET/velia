/*
 * Copyright (C) 2016-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <utility>
#include "HardwareState.h"
#include "utils/log.h"
#include "utils/time.h"

using namespace std::literals;

namespace impl {

std::string ietfHardwareStatePrefix = "/ietf-hardware-state:hardware";

}

namespace velia::hardware {

HardwareState::HardwareState() = default;

HardwareState::~HardwareState() = default;

std::map<std::string, std::string> HardwareState::process()
{
    std::map<std::string, std::string> res;

    for (auto& dataReader : m_callbacks) {
        res.merge(dataReader());
    }

    res[impl::ietfHardwareStatePrefix + "/last-change"] = velia::utils::yangTimeFormat(std::chrono::system_clock::now());
    return res;
}

void HardwareState::registerComponent(DataReader callable)
{
    m_callbacks.push_back(callable);
}

namespace callback {

/** @brief Prefix all properties from values PropertyTree with a component name (calculated from compName) and push them into the PropertyTree */
void addComponent(PropertyTree& res, const std::string& compName, const PropertyTree& values)
{
    const auto componentPrefix = impl::ietfHardwareStatePrefix + "/component[name='" + compName + "']/";

    for (const auto& [k, v] : values) {
        res[componentPrefix + k] = v;
    }
}

/** @brief Write a sensor-data value for a component compName and push it into the PropertyTree. No value conversion is done. */
void addSensorValueRaw(PropertyTree& res, const std::string& compName, const std::string& value)
{
    std::string xpathPrefix = impl::ietfHardwareStatePrefix + "/component[name='" + compName + "']/sensor-data";
    res[xpathPrefix + "/value"] = value;
}

/**
 * @brief Convert a floating point value to integer and push it into the PropertyTree.
 * @pre Requires component[name=compName]/sensor-data/value-precision property to be set in the m_staticData because it is used as a scaling factor when doing the double -> integer conversion
 */
/*
void addSensorValueReal(PropertyTree& res, const std::string& compName, double value)
{
    const std::string xpathPrefix = impl::ietfHardwareStatePrefix + "/component[name='" + compName + "']/sensor-data";
    int8_t precision = std::get<int8_t>(m_staticData.at(xpathPrefix + "/value-precision"));
    addSensorValueRaw(res, compName, value * std::pow(10, precision));
}
*/

Callback::Callback(std::string propertyPrefix, std::string parent)
    : m_propertyPrefix(std::move(propertyPrefix))
    , m_parent(std::move(parent))
{
}

Roadm::Roadm(std::string propertyPrefix, std::string parent)
    : Callback(std::move(propertyPrefix), std::move(parent))
{
    // network element
    addComponent(m_staticData,
                 m_propertyPrefix,
                 PropertyTree {
                     {"class", "iana-hardware:chassis"},
                     {"mfg-name", "CESNET"s}, // FIXME: We have an EEPROM at the PCB for storing these information, but it's so far unused. We could also use U-Boot env variables for this.
                 });
}

PropertyTree Roadm::operator()() const { return m_staticData; }

Controller::Controller(std::string propertyPrefix, std::string parent)
    : Callback(std::move(propertyPrefix), std::move(parent))
{
    // network element
    addComponent(m_staticData,
                 m_propertyPrefix,
                 PropertyTree {
                     {"class", "iana-hardware:module"},
                     {"parent", m_parent},
                 });
}

PropertyTree Controller::operator()() const
{
    return m_staticData;
}

/** @brief Hwmon fan speed callback. Reads from files fanX_input for X from 1 to fansCnt (inclusive). */
Fans::Fans(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fansCnt)
    : Callback(std::move(propertyPrefix), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_fansCnt(fansCnt)
{
    // roadm -> fans
    addComponent(m_staticData,
                 m_propertyPrefix,
                 PropertyTree {
                     {"parent", m_parent},
                     {"class", "iana-hardware:module"}, // FIXME additional props
                 });

    for (unsigned i = 1; i <= m_fansCnt; i++) {
        // roadm -> fan_i
        addComponent(m_staticData,
                     m_propertyPrefix + ":fan" + std::to_string(i),
                     PropertyTree {
                         {"parent", m_propertyPrefix},
                         {"class", "iana-hardware:fan"},
                     });

        // roadm -> fan_i -> input sensor
        addComponent(m_staticData,
                     m_propertyPrefix + ":fan" + std::to_string(i) + ":rpm",
                     PropertyTree {
                         {"parent", m_propertyPrefix},
                         {"class", "iana-hardware:sensor"},
                         {"sensor-data/value-type", "rpm"},
                         {"sensor-data/value-scale", "units"},
                         {"sensor-data/value-precision", "0"},
                         {"sensor-data/oper-status", "ok"},
                     });
    }
}

PropertyTree Fans::operator()() const
{
    PropertyTree res(m_staticData);

    auto attrs = m_hwmon->attributes();

    for (unsigned i = 1; i <= m_fansCnt; i++) {
        addSensorValueRaw(res, m_propertyPrefix + ":fan" + std::to_string(i) + ":rpm", std::to_string(attrs.at("fan"s + std::to_string(i) + "_input")));
    }

    return res;
}

SysfsTemperature::SysfsTemperature(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, int sensorOffset)
    : Callback(std::move(propertyPrefix), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_sensorOffset(sensorOffset)
{
    addComponent(m_staticData,
                 m_propertyPrefix,
                 PropertyTree {
                     {"parent", m_parent},
                     {"class", "iana-hardware:sensor"},
                     {"sensor-data/value-type", "celsius"},
                     {"sensor-data/value-scale", "milli"},
                     {"sensor-data/value-precision", "0"},
                     {"sensor-data/oper-status", "ok"},
                 });
}

PropertyTree SysfsTemperature::operator()() const
{
    PropertyTree res(m_staticData);

    int64_t sensorValue = m_hwmon->attributes().at("temp"s + std::to_string(m_sensorOffset) + "_input");
    addSensorValueRaw(res, m_propertyPrefix, std::to_string(sensorValue));

    return res;
}

EMMC::EMMC(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::EMMC> emmc)
    : Callback(std::move(propertyPrefix), std::move(parent))
    , m_emmc(std::move(emmc))
{
    auto emmcAttrs = m_emmc->attributes();
    // date is specified in MM/YYYY format (source: kernel core/mmc.c) and mfg-date is unfortunately of type yang:date-and-time
    std::string mfgDate = emmcAttrs.at("date");
    mfgDate = mfgDate.substr(3, 4) + "-" + mfgDate.substr(0, 2) + "-01T00:00:00Z";

    addComponent(m_staticData,
                 m_propertyPrefix,
                 PropertyTree {
                     {"parent", m_parent},
                     {"class", "iana-hardware:module"},
                     {"mfg-date", mfgDate},
                     {"serial-num", emmcAttrs.at("serial")},
                     {"model-name", emmcAttrs.at("name")},
                 });

    addComponent(m_staticData,
                 m_propertyPrefix + ":lifetime",
                 PropertyTree {
                     {"parent", m_propertyPrefix},
                     {"class", "iana-hardware:sensor"},
                     {"sensor-data/value-type", "other"},
                     {"sensor-data/value-scale", "units"},
                     {"sensor-data/value-precision", "0"},
                     {"sensor-data/oper-status", "ok"},
                     {"sensor-data/units-display", "percent"s},
                 });
}

PropertyTree EMMC::operator()() const
{
    PropertyTree res(m_staticData);

    auto emmcAttrs = m_emmc->attributes();
    addSensorValueRaw(res, m_propertyPrefix + ":lifetime", emmcAttrs.at("life_time"));

    return res;
}
}
}
