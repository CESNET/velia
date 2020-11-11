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

namespace impl {

static const std::string ietfHardwareStatePrefix = "/ietf-hardware-state:hardware";
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

    res[impl::ietfHardwareStatePrefix + "/last-change"] = velia::utils::yangTimeFormat(std::chrono::system_clock::now());
    return res;
}

void IETFHardware::registerComponent(DataReader callable)
{
    m_callbacks.push_back(callable);
}

namespace component {

namespace impl {

/** @brief Constructs an xpath for a specific component */
std::string xpathForComponent(const std::string& componentName)
{
    return ::impl::ietfHardwareStatePrefix + "/component[name='" + componentName + "']/";
}

/** @brief Prefix all properties from values PropertyTree with a component name (calculated from compName) and push them into the PropertyTree */
void addComponent(DataTree& res, const std::string& componentName, const DataTree& values)
{
    const auto componentPrefix = xpathForComponent(componentName);

    for (const auto& [k, v] : values) {
        res[componentPrefix + k] = v;
    }
}

/** @brief Write a sensor-data value for a component compName and push it into the PropertyTree. No value conversion is done. */
void addSensorValueRaw(DataTree& res, const std::string& componentName, const std::string& value)
{
    const auto componentPrefix = xpathForComponent(componentName);
    res[componentPrefix + "sensor-data/value"] = value;
}

}

Component::Component(std::string propertyPrefix, std::string parent)
    : m_propertyPrefix(std::move(propertyPrefix))
    , m_parent(std::move(parent))
{
}

Roadm::Roadm(std::string propertyPrefix, std::string parent)
    : Component(std::move(propertyPrefix), std::move(parent))
{
    // network element
    impl::addComponent(m_staticData,
                       m_propertyPrefix,
                       DataTree {
                           {"class", "iana-hardware:chassis"},
                           {"mfg-name", "CESNET"s}, // FIXME: We have an EEPROM at the PCB for storing these information, but it's so far unused. We could also use U-Boot env variables for this.
                       });
}

DataTree Roadm::operator()() const { return m_staticData; }

Controller::Controller(std::string propertyPrefix, std::string parent)
    : Component(std::move(propertyPrefix), std::move(parent))
{
    // network element
    impl::addComponent(m_staticData,
                       m_propertyPrefix,
                       DataTree {
                           {"class", "iana-hardware:module"},
                           {"parent", m_parent},
                       });
}

DataTree Controller::operator()() const
{
    return m_staticData;
}

/** @brief Hwmon fan speed callback. Reads from files fanX_input for X from 1 to fanCount (inclusive). */
Fans::Fans(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fanCount)
    : Component(std::move(propertyPrefix), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_fanCount(fanCount)
{
    // roadm -> fans
    impl::addComponent(m_staticData,
                       m_propertyPrefix,
                       DataTree {
                           {"parent", m_parent},
                           {"class", "iana-hardware:module"}, // FIXME additional props
                       });

    for (unsigned i = 1; i <= m_fanCount; i++) {
        // roadm -> fan_i
        impl::addComponent(m_staticData,
                           m_propertyPrefix + ":fan" + std::to_string(i),
                           DataTree {
                               {"parent", m_propertyPrefix},
                               {"class", "iana-hardware:fan"},
                           });

        // roadm -> fan_i -> input sensor
        impl::addComponent(m_staticData,
                           m_propertyPrefix + ":fan" + std::to_string(i) + ":rpm",
                           DataTree {
                               {"parent", m_propertyPrefix},
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

    for (unsigned i = 1; i <= m_fanCount; i++) {
        impl::addSensorValueRaw(res, m_propertyPrefix + ":fan" + std::to_string(i) + ":rpm", std::to_string(attrs.at("fan"s + std::to_string(i) + "_input")));
    }

    return res;
}

SysfsTemperature::SysfsTemperature(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr)
    : Component(std::move(propertyPrefix), std::move(parent))
    , m_hwmon(std::move(hwmon))
    , m_sysfsChannelNr(sysfsChannelNr)
{
    impl::addComponent(m_staticData,
                       m_propertyPrefix,
                       DataTree {
                           {"parent", m_parent},
                           {"class", "iana-hardware:sensor"},
                           {"sensor-data/value-type", "celsius"},
                           {"sensor-data/value-scale", "milli"},
                           {"sensor-data/value-precision", "0"},
                           {"sensor-data/oper-status", "ok"},
                       });
}

DataTree SysfsTemperature::operator()() const
{
    DataTree res(m_staticData);

    int64_t sensorValue = m_hwmon->attributes().at("temp"s + std::to_string(m_sysfsChannelNr) + "_input");
    impl::addSensorValueRaw(res, m_propertyPrefix, std::to_string(sensorValue));

    return res;
}

EMMC::EMMC(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::EMMC> emmc)
    : Component(std::move(propertyPrefix), std::move(parent))
    , m_emmc(std::move(emmc))
{
    auto emmcAttrs = m_emmc->attributes();
    // date is specified in MM/YYYY format (source: kernel core/mmc.c) and mfg-date is unfortunately of type yang:date-and-time
    std::string mfgDate = emmcAttrs.at("date");
    mfgDate = mfgDate.substr(3, 4) + "-" + mfgDate.substr(0, 2) + "-01T00:00:00Z";

    impl::addComponent(m_staticData,
                       m_propertyPrefix,
                       DataTree {
                           {"parent", m_parent},
                           {"class", "iana-hardware:module"},
                           {"mfg-date", mfgDate},
                           {"serial-num", emmcAttrs.at("serial")},
                           {"model-name", emmcAttrs.at("name")},
                       });

    impl::addComponent(m_staticData,
                       m_propertyPrefix + ":lifetime",
                       DataTree {
                           {"parent", m_propertyPrefix},
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
    impl::addSensorValueRaw(res, m_propertyPrefix + ":lifetime", emmcAttrs.at("life_time"));

    return res;
}
}
}
