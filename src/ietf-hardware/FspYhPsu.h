#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "IETFHardware.h"
#include "ietf-hardware/sysfs/HWMon.h"

namespace velia::ietf_hardware {
class TransientI2C {
public:
    TransientI2C(const uint8_t bus, const uint8_t address, const std::string& driverName);
    virtual ~TransientI2C();
    virtual bool isPresent() const;
    virtual void bind() const;
    virtual void unbind() const;
private:
    uint8_t m_address;
    std::string m_driverName;
    std::string m_isPresentPath;
    std::string m_bindPath;
    std::string m_unbindPath;
    std::string m_addressString;
};

/**
 * This class manages two things:
 * 1) dynamic loading/unloading of the driver for the PSUs
 * 2) reading of hwmon values for the PSUs
 */
struct FspYhPsu {
public:
    FspYhPsu(const std::filesystem::path& hwmonDir, const std::string& psuName, std::shared_ptr<TransientI2C> i2c);
    ~FspYhPsu();
    velia::ietf_hardware::DataTree readValues();
private:
    std::mutex m_mtx;
    std::condition_variable m_cond;
    std::thread m_psuWatcher;
    std::atomic<bool> m_exit;
    std::shared_ptr<TransientI2C> m_i2c;

    std::filesystem::path m_hwmonDir;
    std::string m_psuName;

    std::shared_ptr<velia::ietf_hardware::sysfs::HWMon> m_hwmon;
    std::vector<std::function<velia::ietf_hardware::DataTree()>> m_properties;

    void createPower();
};
}
