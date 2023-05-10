#include "trompeloeil_doctest.h"
#include <cstdint>
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/thresholds.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"

using namespace std::literals;

#define THRESHOLD_STATE(RESOURCE, STATE) {"/ietf-hardware:hardware/component[name='" RESOURCE "']/sensor-data/value", STATE}

using velia::ietf_hardware::State;

TEST_CASE("HardwareState")
{
    TEST_INIT_LOGS;
    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    trompeloeil::sequence seq1;
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();

    auto fans = std::make_shared<FakeHWMon>();
    auto sysfsTempCpu = std::make_shared<FakeHWMon>();
    auto sysfsVoltageAc = std::make_shared<FakeHWMon>();
    auto sysfsVoltageDc = std::make_shared<FakeHWMon>();
    auto sysfsPower = std::make_shared<FakeHWMon>();
    auto sysfsCurrent = std::make_shared<FakeHWMon>();
    auto emmc = std::make_shared<FakeEMMC>();

    std::map<std::string, std::string> attributesEMMC;
    std::map<std::string, int64_t> attributesHWMon;

    // initialize all mocks
    attributesEMMC = {
        // FIXME passing initializer_list to macro is hell
        {"date"s, "02/2017"s},
        {"serial"s, "0x00a8808d"s},
        {"name"s, "8GME4R"s},
    };
    FAKE_EMMC(emmc, attributesEMMC);

    std::vector<int64_t> fanValues = {253, 0, 1280, 666};
    REQUIRE_CALL(*fans, attribute("fan1_input"s)).LR_RETURN(fanValues[0]).TIMES(4);
    REQUIRE_CALL(*fans, attribute("fan2_input"s)).LR_RETURN(fanValues[1]).TIMES(4);
    REQUIRE_CALL(*fans, attribute("fan3_input"s)).LR_RETURN(fanValues[2]).TIMES(4);
    REQUIRE_CALL(*fans, attribute("fan4_input"s)).LR_RETURN(fanValues[3]).TIMES(4);

    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).RETURN(41800).TIMES(4);

    REQUIRE_CALL(*sysfsVoltageAc, attribute("in1_input")).RETURN(220000).TIMES(4);
    REQUIRE_CALL(*sysfsVoltageDc, attribute("in1_input")).RETURN(12000).TIMES(4);
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).RETURN(14000000).TIMES(4);
    REQUIRE_CALL(*sysfsCurrent, attribute("curr1_input")).RETURN(200).TIMES(4);

    attributesEMMC = {{"life_time"s, "40"s}};
    FAKE_EMMC(emmc, attributesEMMC).TIMES(4);

    using velia::ietf_hardware::OneThreshold;
    using velia::ietf_hardware::Thresholds;
    using velia::ietf_hardware::data_reader::EMMC;
    using velia::ietf_hardware::data_reader::Fans;
    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;

    // register components into hw state
    ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
    ietfHardware->registerDataReader(Fans("ne:fans", "ne", fans, 4, Thresholds<int64_t>{
                                                                        .criticalLow = std::nullopt,
                                                                        .warningLow = std::nullopt,
                                                                        .warningHigh = OneThreshold<int64_t>{10000, 2000},
                                                                        .criticalHigh = OneThreshold<int64_t>{15000, 2000},
                                                                    }));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageAC>("ne:ctrl:voltage-in", "ne:ctrl", sysfsVoltageAc, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageDC>("ne:ctrl:voltage-out", "ne:ctrl", sysfsVoltageDc, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:ctrl:power", "ne:ctrl", sysfsPower, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Current>("ne:ctrl:current", "ne:ctrl", sysfsCurrent, 1));
    ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc, Thresholds<int64_t>{
                                                                               .criticalLow = OneThreshold<int64_t>{20, 0},
                                                                               .warningLow = OneThreshold<int64_t>{50, 0},
                                                                               .warningHigh = std::nullopt,
                                                                               .criticalHigh = std::nullopt,
                                                                           }));

    /* Some data readers (like our PSU reader, see the FspYhPsu test) may set oper-state to enabled/disabled depending on whether the device is present.
     * If the device is not present we also don't want to report some elements of the data tree that contain a sensor (ne:psu:child in this case).
     * This should also trigger the alarm reporting that the sensor is missing so we should test that the State::NoValue is reported when the sensor disappears.
     */
    struct PsuDataReader {
        const bool& active;

        velia::ietf_hardware::DataTree operator()()
        {
            velia::ietf_hardware::DataTree res = {
                {"/ietf-hardware:hardware/component[name='ne:psu']/class", "iana-hardware:power-supply"},
                {"/ietf-hardware:hardware/component[name='ne:psu']/parent", "ne"},
                {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "disabled"},
            };

            if (active) {
                res["/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state"] = "enabled";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/class"] = "iana-hardware:sensor";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/parent"] = "ne:psu";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state"] = "enabled";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status"] = "ok";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value"] = "20000";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision"] = "0";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale"] = "milli";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type"] = "volts-DC";
            }

            return res;
        }

        velia::ietf_hardware::ThresholdsBySensorPath thresholds() const
        {
            return {{"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value", Thresholds<int64_t>{
                                                                                                     .criticalLow = std::nullopt,
                                                                                                     .warningLow = OneThreshold<int64_t>{10000, 2000},
                                                                                                     .warningHigh = OneThreshold<int64_t>{15000, 2000},
                                                                                                     .criticalHigh = std::nullopt,
                                                                                                 }}};
        }
    };
    bool psuActive = true;
    ietfHardware->registerDataReader(PsuDataReader{psuActive});

    std::map<std::string, std::string> expected = {
        {"/ietf-hardware:hardware/component[name='ne']/class", "iana-hardware:chassis"},
        {"/ietf-hardware:hardware/component[name='ne']/mfg-name", "CESNET"},
        {"/ietf-hardware:hardware/component[name='ne']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:fans']/class", "iana-hardware:module"},
        {"/ietf-hardware:hardware/component[name='ne:fans']/parent", "ne"},
        {"/ietf-hardware:hardware/component[name='ne:fans']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/parent", "ne:fans:fan1"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value", "253"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-type", "rpm"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/parent", "ne:fans:fan2"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-type", "rpm"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/parent", "ne:fans:fan3"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value", "1280"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-type", "rpm"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/parent", "ne:fans:fan4"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value", "666"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-type", "rpm"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl']/parent", "ne"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl']/class", "iana-hardware:module"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value", "41800"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-type", "celsius"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value", "14000000"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value-scale", "micro"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value-type", "watts"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value", "220000"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-type", "volts-AC"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value", "12000"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-type", "volts-DC"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value", "200"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value-type", "amperes"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/class", "iana-hardware:module"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/serial-num", "0x00a8808d"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/mfg-date", "2017-02-01T00:00:00-00:00"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/model-name", "8GME4R"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/parent", "ne:ctrl:emmc"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value", "40"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-type", "other"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/units-display", "percent"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/state/oper-state", "enabled"},

        {"/ietf-hardware:hardware/component[name='ne:psu']/class", "iana-hardware:power-supply"},
        {"/ietf-hardware:hardware/component[name='ne:psu']/parent", "ne"},
        {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/parent", "ne:psu"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state", "enabled"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value", "20000"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type", "volts-DC"},
    };

    {
        auto [data, alarms] = ietfHardware->process();
        data.erase(modulePrefix + "/last-change"); // exclude last-change node
        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:ctrl:current", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:power", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:temperature-cpu", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:voltage-in", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:voltage-out", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:emmc:lifetime", State::WarningLow),
                    THRESHOLD_STATE("ne:fans:fan1:rpm", State::Normal),
                    THRESHOLD_STATE("ne:fans:fan2:rpm", State::Normal),
                    THRESHOLD_STATE("ne:fans:fan3:rpm", State::Normal),
                    THRESHOLD_STATE("ne:fans:fan4:rpm", State::Normal),
                    THRESHOLD_STATE("ne:psu:child", State::WarningHigh),
                });
    }

    fanValues[1] = 11500;
    expected["/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value"] = "11500";
    {
        auto [data, alarms] = ietfHardware->process();
        data.erase(modulePrefix + "/last-change"); // exclude last-change node
        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:fans:fan2:rpm", State::WarningHigh),
                });
    }

    psuActive = false;
    fanValues[1] = 66666;
    fanValues[2] = 0;

    expected.erase("/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/class");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/parent");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale");
    expected.erase("/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type");
    expected["/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state"] = "disabled";
    expected["/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value"] = "66666";
    expected["/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value"] = "0";

    {
        auto [data, alarms] = ietfHardware->process();
        data.erase(modulePrefix + "/last-change"); // exclude last-change node

        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:fans:fan2:rpm", State::CriticalHigh),
                    THRESHOLD_STATE("ne:psu:child", State::NoValue),
                });
    }

    psuActive = true;
    expected["/ietf-hardware:hardware/component[name='ne:psu']/class"] = "iana-hardware:power-supply";
    expected["/ietf-hardware:hardware/component[name='ne:psu']/parent"] = "ne";
    expected["/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state"] = "enabled";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/class"] = "iana-hardware:sensor";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/parent"] = "ne:psu";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state"] = "enabled";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status"] = "ok";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value"] = "20000";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision"] = "0";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale"] = "milli";
    expected["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type"] = "volts-DC";

    {
        auto [data, alarms] = ietfHardware->process();
        data.erase(modulePrefix + "/last-change"); // exclude last-change node

        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:psu:child", State::WarningHigh),
                });
    }
}
