#include "trompeloeil_doctest.h"
#include <cstdint>
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/thresholds.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"

using namespace std::literals;

#define NUKE_LAST_CHANGE(DATA) DATA.erase("/ietf-hardware:hardware/last-change")

#define COMPONENT(RESOURCE) "/ietf-hardware:hardware/component[name='" RESOURCE "']"

#define THRESHOLD_STATE(RESOURCE, STATE) \
    {                                    \
        COMPONENT(RESOURCE)              \
        "/sensor-data/value", STATE      \
    }

using velia::ietf_hardware::State;

TEST_CASE("HardwareState")
{
    TEST_INIT_LOGS;

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

    std::array<int64_t, 4> fanValues = {777, 0, 1280, 666};
    REQUIRE_CALL(*fans, attribute("fan1_input"s)).LR_RETURN(fanValues[0]).TIMES(5);
    REQUIRE_CALL(*fans, attribute("fan2_input"s)).LR_RETURN(fanValues[1]).TIMES(5);
    REQUIRE_CALL(*fans, attribute("fan3_input"s)).LR_RETURN(fanValues[2]).TIMES(5);
    REQUIRE_CALL(*fans, attribute("fan4_input"s)).LR_RETURN(fanValues[3]).TIMES(5);

    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).RETURN(41800).TIMES(5);

    REQUIRE_CALL(*sysfsVoltageAc, attribute("in1_input")).RETURN(220000).TIMES(5);
    REQUIRE_CALL(*sysfsVoltageDc, attribute("in1_input")).RETURN(12000).TIMES(5);
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).RETURN(14000000).TIMES(5);
    REQUIRE_CALL(*sysfsCurrent, attribute("curr1_input")).RETURN(200).TIMES(5);

    attributesEMMC = {{"life_time"s, "40"s}};
    FAKE_EMMC(emmc, attributesEMMC).TIMES(5);

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
                                                                        .criticalLow = OneThreshold<int64_t>{300, 200},
                                                                        .warningLow = OneThreshold<int64_t>{600, 200},
                                                                        .warningHigh = std::nullopt,
                                                                        .criticalHigh = std::nullopt,
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

        velia::ietf_hardware::SensorPollData operator()()
        {
            std::tuple<std::string, std::string, std::string, std::string> alarm;
            velia::ietf_hardware::ThresholdsBySensorPath thr;
            velia::ietf_hardware::DataTree res = {
                {COMPONENT("ne:psu") "/class", "iana-hardware:power-supply"},
                {COMPONENT("ne:psu") "/parent", "ne"},
                {COMPONENT("ne:psu") "/state/oper-state", "disabled"},
            };

            if (active) {
                res[COMPONENT("ne:psu") "/state/oper-state"] = "enabled";
                res[COMPONENT("ne:psu:child") "/class"] = "iana-hardware:sensor";
                res[COMPONENT("ne:psu:child") "/parent"] = "ne:psu";
                res[COMPONENT("ne:psu:child") "/state/oper-state"] = "enabled";
                res[COMPONENT("ne:psu:child") "/sensor-data/oper-status"] = "ok";
                res[COMPONENT("ne:psu:child") "/sensor-data/value"] = "20000";
                res[COMPONENT("ne:psu:child") "/sensor-data/value-precision"] = "0";
                res[COMPONENT("ne:psu:child") "/sensor-data/value-scale"] = "milli";
                res[COMPONENT("ne:psu:child") "/sensor-data/value-type"] = "volts-DC";

                thr[COMPONENT("ne:psu:child") "/sensor-data/value"] = Thresholds<int64_t>{
                    .criticalLow = std::nullopt,
                    .warningLow = OneThreshold<int64_t>{10000, 2000},
                    .warningHigh = OneThreshold<int64_t>{15000, 2000},
                    .criticalHigh = std::nullopt,
                };

                alarm = std::make_tuple("velia-alarms:sensor-missing", COMPONENT("ne:psu"), "cleared", "PSU missing.");
            } else {
                alarm = std::make_tuple("velia-alarms:sensor-missing", COMPONENT("ne:psu"), "warning", "PSU missing.");
            }

            return {res, thr, {alarm}};
        }
    };
    bool psuActive = true;
    ietfHardware->registerDataReader(PsuDataReader{psuActive});

    std::map<std::string, std::string> expected = {
        {COMPONENT("ne") "/class", "iana-hardware:chassis"},
        {COMPONENT("ne") "/mfg-name", "CESNET"},
        {COMPONENT("ne") "/state/oper-state", "enabled"},

        {COMPONENT("ne:fans") "/class", "iana-hardware:module"},
        {COMPONENT("ne:fans") "/parent", "ne"},
        {COMPONENT("ne:fans") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan1") "/class", "iana-hardware:fan"},
        {COMPONENT("ne:fans:fan1") "/parent", "ne:fans"},
        {COMPONENT("ne:fans:fan1") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan1:rpm") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:fans:fan1:rpm") "/parent", "ne:fans:fan1"},
        {COMPONENT("ne:fans:fan1:rpm") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:fans:fan1:rpm", "777"),
        {COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value-scale", "units"},
        {COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value-type", "rpm"},
        {COMPONENT("ne:fans:fan1:rpm") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan2") "/class", "iana-hardware:fan"},
        {COMPONENT("ne:fans:fan2") "/parent", "ne:fans"},
        {COMPONENT("ne:fans:fan2") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan2:rpm") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:fans:fan2:rpm") "/parent", "ne:fans:fan2"},
        {COMPONENT("ne:fans:fan2:rpm") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:fans:fan2:rpm", "0"),
        {COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value-scale", "units"},
        {COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value-type", "rpm"},
        {COMPONENT("ne:fans:fan2:rpm") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan3") "/class", "iana-hardware:fan"},
        {COMPONENT("ne:fans:fan3") "/parent", "ne:fans"},
        {COMPONENT("ne:fans:fan3") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan3:rpm") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:fans:fan3:rpm") "/parent", "ne:fans:fan3"},
        {COMPONENT("ne:fans:fan3:rpm") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:fans:fan3:rpm", "1280"),
        {COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value-scale", "units"},
        {COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value-type", "rpm"},
        {COMPONENT("ne:fans:fan3:rpm") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan4") "/class", "iana-hardware:fan"},
        {COMPONENT("ne:fans:fan4") "/parent", "ne:fans"},
        {COMPONENT("ne:fans:fan4") "/state/oper-state", "enabled"},
        {COMPONENT("ne:fans:fan4:rpm") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:fans:fan4:rpm") "/parent", "ne:fans:fan4"},
        {COMPONENT("ne:fans:fan4:rpm") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:fans:fan4:rpm", "666"),
        {COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value-scale", "units"},
        {COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value-type", "rpm"},
        {COMPONENT("ne:fans:fan4:rpm") "/state/oper-state", "enabled"},

        {COMPONENT("ne:ctrl") "/parent", "ne"},
        {COMPONENT("ne:ctrl") "/class", "iana-hardware:module"},
        {COMPONENT("ne:ctrl") "/state/oper-state", "enabled"},

        {COMPONENT("ne:ctrl:temperature-cpu") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:ctrl:temperature-cpu") "/parent", "ne:ctrl"},
        {COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:ctrl:temperature-cpu", "41800"),
        {COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value-scale", "milli"},
        {COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value-type", "celsius"},
        {COMPONENT("ne:ctrl:temperature-cpu") "/state/oper-state", "enabled"},

        {COMPONENT("ne:ctrl:power") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:ctrl:power") "/parent", "ne:ctrl"},
        {COMPONENT("ne:ctrl:power") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:ctrl:power", "14000000"),
        {COMPONENT("ne:ctrl:power") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:ctrl:power") "/sensor-data/value-scale", "micro"},
        {COMPONENT("ne:ctrl:power") "/sensor-data/value-type", "watts"},
        {COMPONENT("ne:ctrl:power") "/state/oper-state", "enabled"},

        {COMPONENT("ne:ctrl:voltage-in") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:ctrl:voltage-in") "/parent", "ne:ctrl"},
        {COMPONENT("ne:ctrl:voltage-in") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:ctrl:voltage-in", "220000"),
        {COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value-scale", "milli"},
        {COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value-type", "volts-AC"},
        {COMPONENT("ne:ctrl:voltage-in") "/state/oper-state", "enabled"},
        {COMPONENT("ne:ctrl:voltage-out") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:ctrl:voltage-out") "/parent", "ne:ctrl"},
        {COMPONENT("ne:ctrl:voltage-out") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:ctrl:voltage-out", "12000"),
        {COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value-scale", "milli"},
        {COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value-type", "volts-DC"},
        {COMPONENT("ne:ctrl:voltage-out") "/state/oper-state", "enabled"},

        {COMPONENT("ne:ctrl:current") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:ctrl:current") "/parent", "ne:ctrl"},
        {COMPONENT("ne:ctrl:current") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:ctrl:current", "200"),
        {COMPONENT("ne:ctrl:current") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:ctrl:current") "/sensor-data/value-scale", "milli"},
        {COMPONENT("ne:ctrl:current") "/sensor-data/value-type", "amperes"},
        {COMPONENT("ne:ctrl:current") "/state/oper-state", "enabled"},

        {COMPONENT("ne:ctrl:emmc") "/parent", "ne:ctrl"},
        {COMPONENT("ne:ctrl:emmc") "/class", "iana-hardware:module"},
        {COMPONENT("ne:ctrl:emmc") "/serial-num", "0x00a8808d"},
        {COMPONENT("ne:ctrl:emmc") "/mfg-date", "2017-02-01T00:00:00-00:00"},
        {COMPONENT("ne:ctrl:emmc") "/model-name", "8GME4R"},
        {COMPONENT("ne:ctrl:emmc") "/state/oper-state", "enabled"},
        {COMPONENT("ne:ctrl:emmc:lifetime") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:ctrl:emmc:lifetime") "/parent", "ne:ctrl:emmc"},
        {COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:ctrl:emmc:lifetime", "40"),
        {COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value-scale", "units"},
        {COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value-type", "other"},
        {COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/units-display", "percent"},
        {COMPONENT("ne:ctrl:emmc:lifetime") "/state/oper-state", "enabled"},

        {COMPONENT("ne:psu") "/class", "iana-hardware:power-supply"},
        {COMPONENT("ne:psu") "/parent", "ne"},
        {COMPONENT("ne:psu") "/state/oper-state", "enabled"},
        {COMPONENT("ne:psu:child") "/class", "iana-hardware:sensor"},
        {COMPONENT("ne:psu:child") "/parent", "ne:psu"},
        {COMPONENT("ne:psu:child") "/state/oper-state", "enabled"},
        {COMPONENT("ne:psu:child") "/sensor-data/oper-status", "ok"},
        THRESHOLD_STATE("ne:psu:child", "20000"),
        {COMPONENT("ne:psu:child") "/sensor-data/value-precision", "0"},
        {COMPONENT("ne:psu:child") "/sensor-data/value-scale", "milli"},
        {COMPONENT("ne:psu:child") "/sensor-data/value-type", "volts-DC"},
    };

    {
        auto [data, alarms, activeSensors, sideLoadedAlarms] = ietfHardware->process();
        NUKE_LAST_CHANGE(data);
        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:ctrl:current", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:power", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:temperature-cpu", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:voltage-in", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:voltage-out", State::Disabled),
                    THRESHOLD_STATE("ne:ctrl:emmc:lifetime", State::WarningLow),
                    THRESHOLD_STATE("ne:fans:fan1:rpm", State::Normal),
                    THRESHOLD_STATE("ne:fans:fan2:rpm", State::CriticalLow),
                    THRESHOLD_STATE("ne:fans:fan3:rpm", State::Normal),
                    THRESHOLD_STATE("ne:fans:fan4:rpm", State::Normal),
                    THRESHOLD_STATE("ne:psu:child", State::WarningHigh),
                });
        REQUIRE(activeSensors == std::set<std::string>{
                    COMPONENT("ne:ctrl:current") "/sensor-data/value",
                    COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value",
                    COMPONENT("ne:ctrl:power") "/sensor-data/value",
                    COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value",
                    COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value",
                    COMPONENT("ne:psu:child") "/sensor-data/value",
                });
        REQUIRE(sideLoadedAlarms == std::set<std::tuple<std::string, std::string, std::string, std::string>>{{"velia-alarms:sensor-missing", COMPONENT("ne:psu"), "cleared", "PSU missing."}});
    }

    fanValues[1] = 500;
    expected[COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value"] = "500";
    {
        auto [data, alarms, activeSensors, sideLoadedAlarms] = ietfHardware->process();
        NUKE_LAST_CHANGE(data);
        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:fans:fan2:rpm", State::WarningLow),
                });
        REQUIRE(activeSensors == std::set<std::string>{
                    COMPONENT("ne:ctrl:current") "/sensor-data/value",
                    COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value",
                    COMPONENT("ne:ctrl:power") "/sensor-data/value",
                    COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value",
                    COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value",
                    COMPONENT("ne:psu:child") "/sensor-data/value",
                });
        REQUIRE(sideLoadedAlarms == std::set<std::tuple<std::string, std::string, std::string, std::string>>{{"velia-alarms:sensor-missing", COMPONENT("ne:psu"), "cleared", "PSU missing."}});
    }

    psuActive = false;
    fanValues[1] = 1;
    fanValues[2] = 5000;

    expected.erase(COMPONENT("ne:psu:child") "/class");
    expected.erase(COMPONENT("ne:psu:child") "/parent");
    expected.erase(COMPONENT("ne:psu:child") "/state/oper-state");
    expected.erase(COMPONENT("ne:psu:child") "/sensor-data/oper-status");
    expected.erase(COMPONENT("ne:psu:child") "/sensor-data/value");
    expected.erase(COMPONENT("ne:psu:child") "/sensor-data/value-precision");
    expected.erase(COMPONENT("ne:psu:child") "/sensor-data/value-scale");
    expected.erase(COMPONENT("ne:psu:child") "/sensor-data/value-type");
    expected[COMPONENT("ne:psu") "/state/oper-state"] = "disabled";
    expected[COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value"] = "1";
    expected[COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value"] = "5000";

    {
        auto [data, alarms, activeSensors, sideLoadedAlarms] = ietfHardware->process();
        NUKE_LAST_CHANGE(data);

        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:fans:fan2:rpm", State::CriticalLow),
                    THRESHOLD_STATE("ne:psu:child", State::NoValue),
                });
        REQUIRE(activeSensors == std::set<std::string>{
                    COMPONENT("ne:ctrl:current") "/sensor-data/value",
                    COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value",
                    COMPONENT("ne:ctrl:power") "/sensor-data/value",
                    COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value",
                    COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value",
                });
        REQUIRE(sideLoadedAlarms == std::set<std::tuple<std::string, std::string, std::string, std::string>>{{"velia-alarms:sensor-missing", COMPONENT("ne:psu"), "warning", "PSU missing."}});
    }

    psuActive = true;
    expected[COMPONENT("ne:psu") "/class"] = "iana-hardware:power-supply";
    expected[COMPONENT("ne:psu") "/parent"] = "ne";
    expected[COMPONENT("ne:psu") "/state/oper-state"] = "enabled";
    expected[COMPONENT("ne:psu:child") "/class"] = "iana-hardware:sensor";
    expected[COMPONENT("ne:psu:child") "/parent"] = "ne:psu";
    expected[COMPONENT("ne:psu:child") "/state/oper-state"] = "enabled";
    expected[COMPONENT("ne:psu:child") "/sensor-data/oper-status"] = "ok";
    expected[COMPONENT("ne:psu:child") "/sensor-data/value"] = "20000";
    expected[COMPONENT("ne:psu:child") "/sensor-data/value-precision"] = "0";
    expected[COMPONENT("ne:psu:child") "/sensor-data/value-scale"] = "milli";
    expected[COMPONENT("ne:psu:child") "/sensor-data/value-type"] = "volts-DC";

    {
        auto [data, alarms, activeSensors, sideLoadedAlarms] = ietfHardware->process();
        NUKE_LAST_CHANGE(data);

        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:psu:child", State::WarningHigh),
                });
        REQUIRE(activeSensors == std::set<std::string>{
                    COMPONENT("ne:ctrl:current") "/sensor-data/value",
                    COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value",
                    COMPONENT("ne:ctrl:power") "/sensor-data/value",
                    COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value",
                    COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value",
                    COMPONENT("ne:psu:child") "/sensor-data/value",
                });
        REQUIRE(sideLoadedAlarms == std::set<std::tuple<std::string, std::string, std::string, std::string>>{{"velia-alarms:sensor-missing", COMPONENT("ne:psu"), "cleared", "PSU missing."}});
    }


    fanValues[0] = -1'000'000'001;
    fanValues[1] = 1'000'000'001;
    expected[COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value"] = "-1000000000";
    expected[COMPONENT("ne:fans:fan1:rpm") "/sensor-data/oper-status"] = "nonoperational";
    expected[COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value"] = "1000000000";
    expected[COMPONENT("ne:fans:fan2:rpm") "/sensor-data/oper-status"] = "nonoperational";

    {
        auto [data, alarms, activeSensors, sideLoadedAlarms] = ietfHardware->process();
        NUKE_LAST_CHANGE(data);

        REQUIRE(data == expected);
        REQUIRE(alarms == std::map<std::string, velia::ietf_hardware::State>{
                    THRESHOLD_STATE("ne:fans:fan1:rpm", State::CriticalLow),
                    THRESHOLD_STATE("ne:fans:fan2:rpm", State::Normal),
                });
        REQUIRE(activeSensors == std::set<std::string>{
                    COMPONENT("ne:ctrl:current") "/sensor-data/value",
                    COMPONENT("ne:ctrl:emmc:lifetime") "/sensor-data/value",
                    COMPONENT("ne:ctrl:power") "/sensor-data/value",
                    COMPONENT("ne:ctrl:temperature-cpu") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-in") "/sensor-data/value",
                    COMPONENT("ne:ctrl:voltage-out") "/sensor-data/value",
                    COMPONENT("ne:fans:fan1:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan2:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan3:rpm") "/sensor-data/value",
                    COMPONENT("ne:fans:fan4:rpm") "/sensor-data/value",
                    COMPONENT("ne:psu:child") "/sensor-data/value",
                });
        REQUIRE(sideLoadedAlarms == std::set<std::tuple<std::string, std::string, std::string, std::string>>{{"velia-alarms:sensor-missing", COMPONENT("ne:psu"), "cleared", "PSU missing."}});
    }
}
