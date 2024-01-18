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
 * 1) dynamic loading/unloading of the driver for the PDU/PSUs
 * 2) reading of hwmon values for the PDU/PSUs
 *
 * This is only a common part of drivers for PDU and PSUs.
 *
 * @see FspYhPsu
 * @see FspYhPdu
 */
struct FspYh {
public:
    FspYh(const std::filesystem::path& hwmonDir, const std::string& psu, std::shared_ptr<TransientI2C> i2c);
    virtual ~FspYh();
    SensorPollData readValues();

protected:
    std::mutex m_mtx;
    std::condition_variable m_cond;
    std::thread m_psuWatcher;
    std::atomic<bool> m_exit;
    std::shared_ptr<TransientI2C> m_i2c;

    std::filesystem::path m_hwmonDir;

    std::shared_ptr<velia::ietf_hardware::sysfs::HWMon> m_hwmon;

    std::string m_namePrefix;
    velia::ietf_hardware::DataTree m_staticData;

    std::vector<std::function<SensorPollData()>> m_properties;

    virtual void createPower() = 0;
    virtual std::string missingAlarmDescription() const = 0;
};

struct FspYhPsu : public FspYh {
    using FspYh::FspYh;
    void createPower() override;
    std::string missingAlarmDescription() const override;
};

struct FspYhPdu : public FspYh {
    using FspYh::FspYh;
    void createPower() override;
    std::string missingAlarmDescription() const override;
};

}
