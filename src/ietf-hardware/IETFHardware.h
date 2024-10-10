/*
 * Copyright (C) 2016-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"
#include "ietf-hardware/thresholds.h"
#include "utils/log-fwd.h"

using namespace std::literals;

namespace velia::ietf_hardware {

using DataTree = std::map<std::string, std::string>;
using ThresholdsBySensorPath = std::map<std::string, Thresholds<int64_t>>;

struct SideLoadedAlarm {
    std::string alarmTypeId;
    std::string resource;
    std::string severity;
    std::string text;

    auto operator<=>(const SideLoadedAlarm&) const = default;
};

struct HardwareInfo {
    DataTree dataTree;
    std::map<std::string, ThresholdUpdate<int64_t>> updatedTresholdCrossing;
    std::set<std::string> activeSensors;
    std::set<SideLoadedAlarm> sideLoadedAlarms;
};

struct SensorPollData {
    DataTree data;
    ThresholdsBySensorPath thresholds;
    std::set<SideLoadedAlarm> sideLoadedAlarms;
    void merge(SensorPollData&& other);
};

/**
 * @brief Readout of hardware-state related data according to RFC 8348 (App. A)
 *
 * IETFHardware implements readout of various hardware component data and provides them as a mapping
 * nodePath -> value, where nodePath is an XPath conforming to ietf-hardware-state module (RFC 8348, App. A).
 *
 * The class is designed to be modular. IETFHardware does not do much, its only capabilities are:
 *  - Register data readers responsible for readout of data for individual hardware, and
 *  - ask registered data readers to provide the data.
 *
 * The data readers (IETFHardware::DataReader) are simple functors with signature DataTree() (i.e., std::map<std::string, std::string>())
 * returning parts of the tree in the form described above (i.e., mapping nodePath -> value).
 *
 * The IETFHardware is *not aware* of Sysrepo.
 * However, the data readers are aware of the tree structure of the YANG module ietf-hardware-state.
 * That is because they also create the specified parts of the resulting tree.
 *
 * @see IETFHardware::DataReader The data reader.
 * @see velia::ietf_hardware::data_reader Namespace containing several predefined components.
 */
class IETFHardware {

public:
    /** @brief The component */
    using DataReader = std::function<SensorPollData()>;

    IETFHardware();
    ~IETFHardware();

    void registerDataReader(const DataReader& callable);
    HardwareInfo process();

private:
    velia::Log m_log;

    /** @brief registered components for individual modules */
    std::vector<DataReader> m_callbacks;

    /** @brief watchers for any sensor value xPath reported by data readers */
    std::map<std::string, Watcher<int64_t>> m_thresholdsWatchers;
};

/**
 * @brief Read a range of bytes from an EEPROM in hex
 */
std::optional<std::string> hexEEPROM(const std::string& sysfsPrefix, const uint8_t bus, const uint8_t address, const uint32_t totalSize, const uint32_t offset, const uint32_t length);

/**
 * This namespace contains several predefined data readers for IETFHardware.
 * They are implemented as functors and fulfill the required interface -- std::function<DataTree()>
 *
 * The philosophy here is to initialize Component::m_staticData DataTree when constructing the object so the tree does not have to be completely reconstructed every time.
 * When IETFHardwareState fetches the data from the data reader, an operator() is invoked.
 * The dynamic data are fetched by IETFHardware class by the use of the operator().
 * The result of call to operator() will be merged into the static data and returned to caller (IETFHardwareState).
 *
 * Note that a data reader can return any number of nodes and even multiple compoments.
 * For example, Fans data reader will create multiple components in the tree, one for each fan.
 */
namespace data_reader {

struct DataReader {
    /** @brief name of the module component in the tree, e.g. ne:fans:fan1 */
    std::string m_componentName;

    /** @brief name of the parent module */
    std::optional<std::string> m_parent;

    /** @brief static hw-state related data */
    DataTree m_staticData;

    velia::Log m_log;

    DataReader(std::string propertyPrefix, std::optional<std::string> parent);
};

/** @brief Manages any component composing of static data only. The static data are provided as a DataTree in construction time. */
struct StaticData : private DataReader {
    StaticData(std::string propertyPrefix, std::optional<std::string> parent, DataTree tree);
    SensorPollData operator()() const;
};

/** @brief Manages fans component. Data is provided by a sysfs::HWMon object. */
struct Fans : protected DataReader {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    unsigned m_fanChannelsCount;
    Thresholds<int64_t> m_thresholds;

public:
    Fans(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fanChannelsCount, Thresholds<int64_t> thresholds = {});
    SensorPollData operator()() const;
};

/** @brief Wrapper around a hwmon chip which adds extra metadata such as a dynamic S/N from an EEPROM */
struct CzechLightFans : public Fans {
    using SerialNumberCallback = std::function<std::optional<std::string>()>;
private:
    SerialNumberCallback m_serialNumber;
public:
    CzechLightFans(std::string propertyPrefix,
         std::optional<std::string> parent,
         std::shared_ptr<sysfs::HWMon> hwmon,
         unsigned fanChannelsCount,
         Thresholds<int64_t> thresholds,
         const SerialNumberCallback& cbSerialNumber);
    SensorPollData operator()() const;
};

enum class SensorType {
    Temperature,
    Current,
    VoltageDC,
    VoltageAC,
    Power

};

/** @brief Manages a single value from sysfs, data is provided by a sysfs::HWMon object. */
template <SensorType TYPE>
struct SysfsValue : private DataReader {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    std::string m_sysfsFile;
    Thresholds<int64_t> m_thresholds;

public:
    SysfsValue(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr, Thresholds<int64_t> thresholds = {});
    SensorPollData operator()() const;
};

/** @brief Manages a single eMMC block device hardware component. Data is provided by a sysfs::EMMC object. */
struct EMMC : private DataReader {
private:
    std::shared_ptr<sysfs::EMMC> m_emmc;
    Thresholds<int64_t> m_thresholds;

public:
    EMMC(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::EMMC> emmc, Thresholds<int64_t> thresholds = {});
    SensorPollData operator()() const;
};

/** brief Static data and a serial number read from the trailing part of the EEPROM */
struct EepromWithUid : private DataReader {
    EepromWithUid(std::string componentName, std::optional<std::string> parent, const std::string& sysfsPrefix, const uint8_t bus, const uint8_t address, const uint32_t totalSize, const uint32_t offset, const uint32_t length);
    SensorPollData operator()() const;
};

}
}
