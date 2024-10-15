#include "trompeloeil_doctest.h"
#include <fstream>
#include "fs-helpers/utils.h"
#include "ietf-hardware/FspYh.h"
#include "ietf-hardware/IETFHardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/sysrepo-helpers/common.h"

using namespace std::literals;

class FakeI2C : public velia::ietf_hardware::TransientI2C {
public:
    FakeI2C(const std::filesystem::path& fakeSysfsDeviceEntry)
        : TransientI2C({}, {}, {})
        , m_fakeSysfsDeviceEntry(fakeSysfsDeviceEntry)
    {
    }

    MAKE_CONST_MOCK0(isPresent, bool(), override);
    MAKE_CONST_MOCK0(bind_mock, void());
    MAKE_CONST_MOCK0(unbind_mock, void());

    void removeHwmonFile(const std::string& name) const
    {
        std::filesystem::remove(m_fakeSysfsDeviceEntry / ("hwmon" + std::to_string(m_hwmonNo)) / name);
    }

    void bind() const override
    {
        bind_mock();
        removeDirectoryTreeIfExists(m_fakeSysfsDeviceEntry);
        std::filesystem::create_directory(m_fakeSysfsDeviceEntry);
        std::filesystem::create_directory(m_fakeSysfsDeviceEntry / "hwmon");
        std::filesystem::create_directory(m_fakeSysfsDeviceEntry / "hwmon" / ("hwmon" + std::to_string(m_hwmonNo)));

        for (const auto& filename : {"name", "temp1_input", "temp2_input", "curr1_input", "curr2_input", "curr3_input", "in1_input", "in2_input", "in3_input", "power1_input", "power2_input", "fan1_input"}) {
            std::ofstream ofs(m_fakeSysfsDeviceEntry / "hwmon" / ("hwmon" + std::to_string(m_hwmonNo)) / filename);
            // I don't really care about the values here, I just need the HWMon class to think that the files exist.
            ofs << 0 << "\n";
        }
    }
    void unbind() const override
    {
        unbind_mock();
        removeDirectoryTreeIfExists(m_fakeSysfsDeviceEntry);
        m_hwmonNo++;
    }
    std::filesystem::path sysfsEntry() const override
    {
        return m_fakeSysfsDeviceEntry;
    }

private:
    std::filesystem::path m_fakeSysfsDeviceEntry;
    mutable std::atomic<int> m_hwmonNo = 1;
};

TEST_CASE("FspYhPsu")
{
    TEST_INIT_LOGS;
    std::atomic<int> counter = 0;
    const auto fakeSysfsDeviceEntry = CMAKE_CURRENT_BINARY_DIR + "/tests/psu"s;
    removeDirectoryTreeIfExists(fakeSysfsDeviceEntry);
    auto fakePMBus = std::make_shared<FakeI2C>(fakeSysfsDeviceEntry);
    trompeloeil::sequence seq1;
    std::shared_ptr<velia::ietf_hardware::FspYhPsu> psu;
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

    auto i2cPresence = [&counter] {
        switch (counter) {
        case 0:
        case 2:
        case 4:
            return false;
        case 1:
        case 3:
            return true;
        }

        REQUIRE(false);
        __builtin_unreachable();
    };

    ALLOW_CALL(*fakePMBus, isPresent()).LR_RETURN(i2cPresence());
    REQUIRE_CALL(*fakePMBus, bind_mock()).LR_WITH(counter == 1).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakePMBus, unbind_mock()).LR_WITH(counter == 2).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakePMBus, bind_mock()).LR_WITH(counter == 3).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakePMBus, unbind_mock()).LR_WITH(counter == 4).IN_SEQUENCE(seq1);

    psu = std::make_shared<velia::ietf_hardware::FspYhPsu>("psu", fakePMBus);

    const velia::ietf_hardware::DataTree expectedDisabled = {
        {"/ietf-hardware:hardware/component[name='ne:psu']/class", "iana-hardware:power-supply"},
        {"/ietf-hardware:hardware/component[name='ne:psu']/parent", "ne"},
        {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "disabled"}};

    const velia::ietf_hardware::SideLoadedAlarm alarmUnplugged = {"velia-alarms:sensor-missing-alarm", "/ietf-hardware:hardware/component[name='ne:psu']", "critical", "PSU is unplugged."};
    const velia::ietf_hardware::SideLoadedAlarm alarmPlugged = {"velia-alarms:sensor-missing-alarm", "/ietf-hardware:hardware/component[name='ne:psu']", "cleared", "PSU is unplugged."};

    std::set<std::string> expectedThresholdsKeys;

    for (auto i : {0, 1, 2, 3, 4}) {
        std::this_thread::sleep_for(std::chrono::seconds(4));
        velia::ietf_hardware::DataTree expected;
        std::set<velia::ietf_hardware::SideLoadedAlarm> expectedAlarms;

        switch (i) {
        case 0:
            expected = expectedDisabled;
            expectedThresholdsKeys.clear();
            expectedAlarms = {alarmUnplugged};
            break;
        case 1:
            expected = {
                {"/ietf-hardware:hardware/component[name='ne:psu']/class", "iana-hardware:power-supply"},
                {"/ietf-hardware:hardware/component[name='ne:psu']/parent", "ne"},
                {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value-type", "amperes"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value-type", "amperes"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value-type", "amperes"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1']/class", "iana-hardware:fan"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1']/parent", "ne:psu:fan"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/parent", "ne:psu:fan:fan1"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value-scale", "units"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value-type", "rpm"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value-scale", "micro"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value-type", "watts"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value-scale", "micro"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value-type", "watts"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value-type", "celsius"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value-type", "celsius"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value-type", "volts-DC"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value-type", "volts-DC"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/state/oper-state", "enabled"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value-type", "volts-AC"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/state/oper-state", "enabled"},
            };
            expectedThresholdsKeys = {
                "/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value",
                "/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value",
            };
            expectedAlarms = {alarmPlugged};
            break;
        case 2:
            expected = expectedDisabled;
            expectedThresholdsKeys.clear();
            expectedAlarms = {alarmUnplugged};
            break;
        case 3:
            // Here I simulate read failure by a file from the hwmon directory. This happens when the user wants data from
            // a PSU that's no longer there and the watcher thread didn't unbind it yet.
            fakePMBus->removeHwmonFile("temp1_input");
            expected = expectedDisabled;
            expectedThresholdsKeys.clear();
            expectedAlarms = {alarmUnplugged};
            break;
        case 4:
            expected = expectedDisabled;
            expectedThresholdsKeys.clear();
            expectedAlarms = {alarmUnplugged};
            break;
        }

        auto [data, thresholds, sideLoadedAlarms] = psu->readValues();

        CAPTURE((int)counter);
        REQUIRE(data == expected);

        std::set<std::string> thresholdsKeys;
        std::transform(thresholds.begin(), thresholds.end(), std::inserter(thresholdsKeys, thresholdsKeys.begin()), [](const auto& kv) { return kv.first; });
        REQUIRE(thresholdsKeys == expectedThresholdsKeys);

        REQUIRE(sideLoadedAlarms == expectedAlarms);

        counter++;
    }

    waitForCompletionAndBitMore(seq1);
}
