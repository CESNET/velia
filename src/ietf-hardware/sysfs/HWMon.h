/*
 * Copyright (C) 2017-2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#pragma once

#include <filesystem>
#include <map>
#include <vector>
#include "utils/log-fwd.h"

namespace velia::ietf_hardware::sysfs {

class HWMon {
public:
    using Attributes = std::map<std::string, int64_t>;

    explicit HWMon(std::filesystem::path hwmonDir);
    virtual ~HWMon();

    virtual Attributes attributes() const;
    virtual int64_t attribute(const std::string& name) const;

private:
    velia::Log m_log;

    /** @brief path to the real hwmon directory */
    std::filesystem::path m_root;

    /** @brief keys of entries that are exported via this hwmon. Filled by constructor. */
    std::vector<std::string> m_properties;
};
}
