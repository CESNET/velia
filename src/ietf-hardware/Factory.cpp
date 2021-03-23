#include <fcntl.h>
#include <fstream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include "Factory.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"
#include "utils/log.h"
#include "utils/UniqueResource.h"

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
    DataTree readValues();
private:
    std::mutex m_mtx;
    std::thread m_psuWatcher;
    std::atomic<bool> m_exit;

    uint8_t m_i2cBus;
    uint8_t m_i2cAddress;
    std::filesystem::path m_hwmonDir;
    std::string m_psuName;

    std::shared_ptr<sysfs::HWMon> m_hwmon;
    std::vector<std::function<DataTree()>> m_properties;

    void createPower();
};

void FspYhPsu::createPower()
{
    m_hwmon = std::make_shared<sysfs::HWMon>(m_hwmonDir);
    const auto prefix = "ne:"s + m_psuName;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;
    using velia::ietf_hardware::data_reader::Fans;
    using velia::ietf_hardware::data_reader::SensorType;
    m_properties.push_back(StaticData(prefix, "ne", {{"class", "iana-hardware:power-supply"}}));
    m_properties.push_back(SysfsValue<SensorType::Temperature>(prefix + ":temperature-1", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::Temperature>(prefix + ":temperature-2", prefix, m_hwmon, 2));
    m_properties.push_back(SysfsValue<SensorType::Current>(prefix + ":current-in", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::Current>(prefix + ":current-12V", prefix, m_hwmon, 2));
    m_properties.push_back(SysfsValue<SensorType::Current>(prefix + ":current-5Vsb", prefix, m_hwmon, 3));
    m_properties.push_back(SysfsValue<SensorType::VoltageAC>(prefix + ":voltage-in", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::VoltageDC>(prefix + ":voltage-12V", prefix, m_hwmon, 2));
    m_properties.push_back(SysfsValue<SensorType::VoltageDC>(prefix + ":voltage-5Vsb", prefix, m_hwmon, 3));
    m_properties.push_back(SysfsValue<SensorType::Power>(prefix + ":power-in", prefix, m_hwmon, 1));
    m_properties.push_back(SysfsValue<SensorType::Power>(prefix + ":power-out", prefix, m_hwmon, 2));
    m_properties.push_back(Fans(prefix + ":fan", prefix, m_hwmon, 1));
}

bool i2cDevPresent(const std::string& devicePath, const uint8_t address)
{
    auto file = open(devicePath.c_str(), O_RDWR);
    if (file < 0) {
        throw std::runtime_error("i2cDevPresent: "s + strerror(errno));
    }

    auto fdClose = make_unique_resource([] {}, [file] {
        close(file);
    });

    if (ioctl(file, I2C_SLAVE_FORCE, address) < 0) {
        throw std::runtime_error("i2cDevPresent: "s + strerror(errno));
    }

    char bufferIn[1];
    return read(file, bufferIn, 1) != -1;
}

void i2cLoadDriver(const std::string& i2cDevice, const std::string& address)
{
    std::ofstream ofs("/sys/bus/i2c/devices/i2c-" + i2cDevice + "/new_device");
    ofs << "fsp3y_ym2151e " + address;
}

void i2cUnloadDriver(const std::string& i2cBus, const std::string& address)
{
    std::ofstream ofs("/sys/bus/i2c/devices/i2c-" + i2cBus + "/delete_device");
    ofs << address;
}

FspYhPsu::FspYhPsu(const uint8_t i2cBus, const uint8_t i2cAddress, const std::filesystem::path& hwmonDir, const std::string& psuName)
    : m_i2cBus(i2cBus)
    , m_i2cAddress(i2cAddress)
    , m_hwmonDir(hwmonDir)
    , m_psuName(psuName)
{
    m_exit = false;
    m_psuWatcher = std::thread([this] {
        auto busString = std::to_string(m_i2cBus);
        std::ostringstream addressString;
        addressString << std::showbase << std::hex << m_i2cAddress;

        while (!m_exit) {
            if (i2cDevPresent("/dev/i2c-" + busString, m_i2cAddress)) {
                if (!std::filesystem::is_directory(m_hwmonDir)) {
                    spdlog::get("hardware")->info("Registering PSU at {}", addressString.str());
                    i2cLoadDriver(busString, addressString.str());
                }

                // The driver might already be loaded before the program starts. This ensures that the properties still
                // get initialized if that's the case.
                if (!m_hwmon) {
                    std::lock_guard lk(m_mtx);
                    createPower();
                }
            } else if (std::filesystem::is_directory(m_hwmonDir)) {
                {
                    std::lock_guard lk(m_mtx);
                    m_hwmon = nullptr;
                    m_properties.clear();
                }
                spdlog::get("hardware")->info("Deregistering PSU from {}", addressString.str());
                i2cLoadDriver(busString, addressString.str());
            }

            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    });
}

FspYhPsu::~FspYhPsu()
{
    m_exit = true;
    m_psuWatcher.join();
}

DataTree FspYhPsu::readValues()
{
    std::map<std::string, std::string> res;

    std::lock_guard lk(m_mtx);
    for (auto& reader : m_properties) {
        res.merge(reader());
    }

    return res;
}

std::shared_ptr<IETFHardware> create(const std::string& applianceName)
{
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();

    if (applianceName == "czechlight-clearfog") {
        auto hwmonFans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-002e/hwmon/");
        auto sysfsTempFront = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1011100.i2c/i2c-1/1-002e/hwmon/");
        auto sysfsTempCpu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/virtual/thermal/thermal_zone0/");
        auto sysfsTempMII0 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:00/hwmon/");
        auto sysfsTempMII1 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:01/hwmon/");
        auto emmc = std::make_shared<velia::ietf_hardware::sysfs::EMMC>("/sys/block/mmcblk0/device/");

        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne", std::nullopt, {{"description", "Czechlight project"s}}));
        using velia::ietf_hardware::data_reader::StaticData;
        using velia::ietf_hardware::data_reader::Fans;
        using velia::ietf_hardware::data_reader::SysfsValue;
        using velia::ietf_hardware::data_reader::EMMC;
        using velia::ietf_hardware::data_reader::SensorType;
        ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
        ietfHardware->registerDataReader(Fans("ne:fans", "ne", hwmonFans, 4));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
        ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));
    } else if (applianceName == "czechlight-clearfog-g2") {
        auto fans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-0020/hwmon/");
        auto tempMainBoard = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-0048/hwmon/");
        auto tempFans = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/bus/i2c/devices/1-0049/hwmon/");
        auto tempCpu = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/virtual/thermal/thermal_zone0/");
        auto tempMII0 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:00/hwmon/");
        auto tempMII1 = std::make_shared<velia::ietf_hardware::sysfs::HWMon>("/sys/devices/platform/soc/soc:internal-regs/f1072004.mdio/mdio_bus/f1072004.mdio-mii/f1072004.mdio-mii:01/hwmon/");
        auto emmc = std::make_shared<velia::ietf_hardware::sysfs::EMMC>("/sys/block/mmcblk0/device/");

        using velia::ietf_hardware::data_reader::StaticData;
        using velia::ietf_hardware::data_reader::Fans;
        using velia::ietf_hardware::data_reader::SysfsValue;
        using velia::ietf_hardware::data_reader::EMMC;
        using velia::ietf_hardware::data_reader::SensorType;
        /* FIXME:
         * Publish more properties for ne element. We have an EEPROM at the PCB for storing serial numbers (etc.), but it's so far unused. We could also use U-Boot env variables
         * This will be needed for sdn-roadm-line only. So we should also parse the model from /proc/cmdline here
         */
        ietfHardware->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne", std::nullopt, {{"description", "Czechlight project"s}}));

        ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
        ietfHardware->registerDataReader(Fans("ne:fans", "ne", fans, 4));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-front", "ne:ctrl", tempMainBoard, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", tempCpu, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-rear", "ne:ctrl", tempFans, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-0", "ne:ctrl", tempMII0, 1));
        ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-1", "ne:ctrl", tempMII1, 1));
        ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

        ietfHardware->registerDataReader([psu = std::make_shared<FspYhPsu>(2, 0x58, "/sys/bus/i2c/devices/2-0058/hwmon", "psu1")] {
            return psu->readValues();
        });
        ietfHardware->registerDataReader([psu = std::make_shared<FspYhPsu>(2, 0x59, "/sys/bus/i2c/devices/2-0059/hwmon", "psu2")] {
            return psu->readValues();
        });
    } else {
        throw std::runtime_error("Unknown appliance '" + applianceName + "'");
    }

    return ietfHardware;
}

}
