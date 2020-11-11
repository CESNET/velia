/*
 * Copyright (C) 2016-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <functional>
#include <map>
#include <sysfs/HWMon.h>
#include <utility>
#include "sysfs/EMMC.h"
#include "utils/log-fwd.h"

using PropertyTree = std::map<std::string, std::string>; // FIXME

using namespace std::literals;

namespace velia::hardware {

/**
 * @short Readout of hardware-state related data according to RFC 8348 (App. A)
 */
class HardwareState {
public:
    using DataReader = std::function<PropertyTree()>;

    HardwareState();
    ~HardwareState();

    void registerComponent(DataReader callable);

    std::map<std::string, std::string> process();

private:
    /** @brief registered components for individual modules */
    std::vector<DataReader> m_callbacks;
};

namespace callback {

/**
 * @brief Callback for a single hardware state module.
 *
 * The static (non-changing) data should be loaded into the m_staticData member variable.
 * The readout data are fetched by HardwareState class using the call to the operator(). The result of call to operator() should be merged static data with the variable data.
 */
struct Callback {
    /** @brief name of the module component in the tree, e.g. ne:edfa */
    std::string m_propertyPrefix;

    /** @brief name of the parent module, empty if no parent */
    std::string m_parent;

    /** @brief static hw-state related data */
    PropertyTree m_staticData;

    Callback(std::string propertyPrefix, std::string parent);
};

struct Roadm : private Callback {
    Roadm(std::string propertyPrefix, std::string parent);
    PropertyTree operator()() const;
};

struct Controller : private Callback {
    Controller(std::string propertyPrefix, std::string parent);
    PropertyTree operator()() const;
};

struct Fans : private Callback {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    unsigned m_fansCnt;

public:
    Fans(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, unsigned fansCnt);
    PropertyTree operator()() const;
};

struct SysfsTemperature : private Callback {
private:
    std::shared_ptr<sysfs::HWMon> m_hwmon;
    int m_sensorOffset;

public:
    SysfsTemperature(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::HWMon> hwmon, int sensorOffset);
    PropertyTree operator()() const;
};

struct EMMC : private Callback {
private:
    std::shared_ptr<sysfs::EMMC> m_emmc;

public:
    EMMC(std::string propertyPrefix, std::string parent, std::shared_ptr<sysfs::EMMC> emmc);
    PropertyTree operator()() const;
};
}
}
