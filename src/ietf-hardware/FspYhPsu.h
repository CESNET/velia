#include <atomic>
#include <mutex>
#include <thread>
#include "IETFHardware.h"
#include "ietf-hardware/sysfs/HWMon.h"

namespace velia::ietf_hardware {
/**
 * This class manages two things:
 * 1) dynamic loading/unloading of the driver for the PSUs
 * 2) reading of hwmon values for the PSUs
 */
struct FspYhPsu {
public:
    FspYhPsu(const uint8_t i2cBus, const uint8_t i2cAddress, const std::filesystem::path& hwmonDir, const std::string& psuName);
    ~FspYhPsu();
    velia::ietf_hardware::DataTree readValues();
private:
    std::mutex m_mtx;
    std::thread m_psuWatcher;
    std::atomic<bool> m_exit;

    uint8_t m_i2cBus;
    uint8_t m_i2cAddress;
    std::filesystem::path m_hwmonDir;
    std::string m_psuName;

    std::shared_ptr<velia::ietf_hardware::sysfs::HWMon> m_hwmon;
    std::vector<std::function<velia::ietf_hardware::DataTree()>> m_properties;

    void createPower();
};
}
