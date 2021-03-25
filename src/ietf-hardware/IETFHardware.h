/*
 * Copyright (C) 2016-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <functional>
#include <map>
#include <utility>
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"
#include "utils/log-fwd.h"

using namespace std::literals;

namespace velia::ietf_hardware {

using DataTree = std::map<std::string, std::string>;

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
    using DataReader = std::function<DataTree()>;

    IETFHardware();
    ~IETFHardware();
    void registerDataReader(const DataReader& callable);

    DataTree process();

private:
    /** @brief registered components for individual modules */
    std::vector<DataReader> m_callbacks;
};

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

    DataReader(std::string propertyPrefix, std::optional<std::string> parent);
};

/** @brief Manages any component composing of static data only. The static data are provided as a DataTree in construction time. */
struct StaticData : private DataReader {
    StaticData(std::string propertyPrefix, std::optional<std::string> parent, DataTree tree);
    DataTree operator()() const;
};

/** @brief Manages fans component. Data is provided by a sysfs::HWMon object. */
struct Fans : private DataReader {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    unsigned m_fanChannelsCount;

public:
    Fans(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fanChannelsCount);
    DataTree operator()() const;
};

/** @brief Manages a single temperature sensor, data is provided by a sysfs::HWMon object. */
struct SysfsTemperature : private DataReader {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    std::string m_sysfsTemperatureFile;

public:
    SysfsTemperature(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr);
    DataTree operator()() const;
};

/** @brief Manages a single current sensor, data is provided by a sysfs::HWMon object. */
struct SysfsCurrent : private DataReader {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    std::string m_sysfsCurrentFile;

public:
    SysfsCurrent(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr);
    DataTree operator()() const;
};

/** @brief Manages a single power sensor, data is provided by a sysfs::HWMon object. */
struct SysfsPower : private DataReader {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    std::string m_sysfsPowerFile;

public:
    SysfsPower(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr);
    DataTree operator()() const;
};

// FIXME: could probably merge these Sysfs* classes later
/** @brief Manages a single voltage sensor, data is provided by a sysfs::HWMon object. */
struct SysfsVoltage : private DataReader {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    std::string m_sysfsVoltageFile;

public:
    enum Type {
        AC,
        DC
    };

    SysfsVoltage(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr, const Type type);
    DataTree operator()() const;
};

/** @brief Manages a single eMMC block device hardware component. Data is provided by a sysfs::EMMC object. */
struct EMMC : private DataReader {
private:
    std::shared_ptr<sysfs::EMMC> m_emmc;

public:
    EMMC(std::string propertyPrefix, std::optional<std::string> parent, std::shared_ptr<sysfs::EMMC> emmc);
    DataTree operator()() const;
};
}
}
