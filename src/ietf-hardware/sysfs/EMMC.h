/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#pragma once

#include <filesystem>
#include <map>
#include "SysfsAttributes.h"
#include "utils/log-fwd.h"

namespace velia::ietf_hardware::sysfs {

class EMMC {
public:
    explicit EMMC(std::filesystem::path hwmonDir);
    virtual ~EMMC();

    virtual EMMCAttributes attributes() const;

private:
    velia::Log m_log;

    /** @brief path to the emmc sysfs directory */
    std::filesystem::path m_root;
};
}
