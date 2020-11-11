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
 * @class HardwareState
 * @brief Readout of hardware-state related data according to RFC 8348 (App. A)
 *
 * HardwareState implements readout of various hardware component data and provides them as a mapping
 * nodePath -> value, where nodePath is a xpath conforming to ietf-hardware-state module (RFC 8348, App. A).
 *
 * The class is designed to be modular. HardwareState does not do much, its only capabilities are:
 *  - Register components responsible for readout of data for individual components, and
 *  - ask registered components to provide the data.
 *
 * The components (@see HardwareState::DataReader) are simple functors with signature std::map<std::string, std::string>()
 * returning parts of the tree in the form described above (i.e., mapping nodePath -> value).
 *
 * The HardwareState is *not aware* of Sysrepo. However, it is aware of the tree structure of the YANG ietf-hardware-state
 * module.
 */
class IETFHardware {

public:
    using DataReader = std::function<DataTree()>;

    IETFHardware();
    ~IETFHardware();
    void registerComponent(DataReader callable);

    DataTree process();

private:
    /** @brief registered components for individual modules */
    std::vector<DataReader> m_callbacks;
};

namespace component {

/**
 * @brief Data provider for a single hardware state module.
 *
 * The static (non-changing) data should be loaded into the m_staticData member variable and it will be fetched only once (when the component is registered).
 * The dynamic data are fetched by HardwareState class by the use of the operator().
 * The result of call to operator() will be merged into the static data.
 */
struct Component {
    /** @brief name of the module component in the tree, e.g. ne:edfa */
    std::string m_propertyPrefix;

    /** @brief name of the parent module, empty if no parent */
    std::string m_parent;

    /** @brief static hw-state related data */
    DataTree m_staticData;

    Component(std::string propertyPrefix, std::string parent);
};

struct Roadm : private Component { // FIXME: decide how to change this. It definitely wont be in production. Maybe redesign into a "generic static-data-only node"?
    Roadm(std::string propertyPrefix, std::string parent);
    DataTree operator()() const;
};

struct Controller : private Component {
    Controller(std::string propertyPrefix, std::string parent);
    DataTree operator()() const;
};

struct Fans : private Component {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    unsigned m_fanCount;

public:
    Fans(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fansCnt);
    DataTree operator()() const;
};

struct SysfsTemperature : private Component {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    int m_sysfsChannelNr;

public:
    SysfsTemperature(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, int sysfsChannelNr);
    DataTree operator()() const;
};

struct EMMC : private Component {
private:
    std::shared_ptr<sysfs::EMMC> m_emmc;

public:
    EMMC(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::EMMC> emmc);
    DataTree operator()() const;
};
}
}
