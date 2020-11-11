/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#pragma once

#include <filesystem>
#include <map>
#include "utils/log-fwd.h"

namespace velia::hardware::sysfs {

class EMMC {
public:
    EMMC(std::filesystem::path hwmonDir);
    ~EMMC();

    virtual std::map<std::string, std::string> attributes() const;

private:
    velia::Log m_log;
    /** @brief path to the emmc sysfs directory */
    std::filesystem::path m_root;
};
}
