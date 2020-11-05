/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include "EMMC.h"
#include "utils/io.h"
#include "utils/log.h"

namespace velia::ietf_hardware::sysfs {

/** @class EMMC

@short Implements access to EMMC specific data from sysfs

This class provides EMMC (v5 +) specific data from sysfs
Code based on eMMC 5.1 docs https://www.jedec.org/sites/default/files/docs/JESD84-B51.pdf and kernel code from drivers/mmc/core/mmc.c.
*/

using namespace std::literals;

namespace impl {

/** @brief Report life time of the emmc device. This is a property constructed from 'life_time' and 'pre_eol_information' values reported by kernel.
 *
 *  Kernel provides three different health information values w.r.t. the eMMC standard (>=5, https://www.jedec.org/sites/default/files/docs/JESD84-B51.pdf):
 * - "Device life time estimation type A" (file 'life_time', first hex-encoded value)
 * - "Device life time estimation type B" (file 'life_time', second hex-encoded value)
 * - "Pre EOL information" (file 'pre_eol_information', single hex-encoded value)
 *
 * The first and second values provide an estimated indication about the device life time that is reflected by the averaged wear out of memory of
 * type A (SLC) and type B (MLC) relative to its maximum estimated device life time.
 *  - 0x01...0x0A - correspond to % of lifetime used: 0x01 is 0-10%, 0x02 is 10-20%, ... , 0x0A is 90% - 100%
 *  - 0x0B is over 100 %
 *  - 0x00 is undefined
 *
 *  Both values are always reported according to kernel code (drivers/mmc/core/mmc.c). The standard does not say anything regarding why both are reported.
 *
 *  The EOL information provides indication about device life time reflected by average reserved blocks
 *  - 0x00 is undefined
 *  - 0x01 is normal
 *  - 0x02 is a warning - consumed 80 % of reserved blocks
 *  - 0x03 is urgent (not stated in the linked PDF but sometimes referred as 90 % used)
 *
 * We have decided to merge these values into one (so that customer does not have to be an eMMC expert) percentual value about health.
 * Therefore we report maximum of those percentual values.
 */
std::string processLifeTimeProperty(const std::filesystem::path& rootDir)
{
    std::vector<uint32_t> lifeTime = velia::utils::readFileWords(rootDir / "life_time", 2);
    std::vector<uint32_t> eol = velia::utils::readFileWords(rootDir / "pre_eol_info", 1);
    std::vector<uint32_t> res;

    // interpret lifeTimeData as percentual health
    res.push_back(lifeTime[0] != 0x00 ? (lifeTime[0] - 1) * 10 : 0x00);
    res.push_back(lifeTime[1] != 0x00 ? (lifeTime[1] - 1) * 10 : 0x00);

    // interpret eol data as percentual health
    switch (eol[0]) {
    case 0x00:
    case 0x01:
        res.push_back(0);
        break;
    case 0x02:
        res.push_back(80);
        break;
    case 0x03:
        res.push_back(90);
        break;
    }

    return std::to_string(*std::max_element(res.begin(), res.end()));
}
}

/**
 * @short Constructs a EMMC driver for EMMC entries
 * @param root A path to the EMMC device, e.g., /sys/block/mmcblk0/device
 * */
EMMC::EMMC(std::filesystem::path blockDevDir)
    : m_log(spdlog::get("hardware"))
    , m_root(std::move(blockDevDir))
{
    m_log->trace("EMMC driver initialized for '{}'", std::string(m_root));
}

EMMC::~EMMC() = default;

/** @short Return attributes provided by EMMC sysfs driver */
EMMCAttributes EMMC::attributes() const
{
    static const std::vector<std::string> ATTRIBUTE_FILES = {"serial", "date", "name"};
    EMMCAttributes result;

    for (const auto& filename : ATTRIBUTE_FILES) {
        result.insert(std::make_pair(filename, velia::utils::readFileString(m_root / filename)));
    };
    result.emplace("life_time", impl::processLifeTimeProperty(m_root));

    return result;
}
}
