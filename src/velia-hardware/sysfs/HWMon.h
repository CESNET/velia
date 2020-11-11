/*
 * Copyright (C) 2017-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 *
*/

#pragma once

#include <filesystem>
#include <map>
#include <vector>
#include "utils/log-fwd.h"

namespace velia::hardware::sysfs {

class HWMon {
public:
    HWMon(std::filesystem::path hwmonDir);
    virtual ~HWMon();

    virtual std::map<std::string, int64_t> attributes() const;

private:
    velia::Log m_log;

    /** @brief path to the real hwmon directory */
    std::filesystem::path m_root;

    /** @brief keys of entries that are exported via this hwmon. Filled by constructor. */
    std::vector<std::string> m_properties;
};
}
