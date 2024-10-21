#include "trompeloeil_doctest.h"
#include <iterator>
#include <sysrepo-cpp/Enum.hpp>
#include <trompeloeil.hpp>
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "sysrepo-helpers/alarms.h"
#include "sysrepo-helpers/datastore.h"
#include "test_log_setup.h"
#include "tests/sysrepo-helpers/common.h"
#include "utils/libyang.h"

using namespace std::literals;

#define COMPONENT(RESOURCE) "/ietf-hardware:hardware/component[name='" RESOURCE "']"

#define VEC(...) (std::vector<std::string>{__VA_ARGS__})
#define ALARMS(...) VEC(__VA_ARGS__)
#define COMPONENTS(...) VEC(__VA_ARGS__)

#define INTRODUCED_ALARM(ALARM_TYPE, DESCRIPTION) velia::alarms::AlarmInventoryEntry(ALARM_TYPE, DESCRIPTION)
#define REQUIRE_ALARM_INVENTORY_ADD_ALARMS(...) REQUIRE_NEW_ALARM_INVENTORY_ENTRIES(alarmWatcher, (std::vector<velia::alarms::AlarmInventoryEntry>{__VA_ARGS__}))
#define REQUIRE_ALARM_INVENTORY_ADD_RESOURCES(ALARMS, COMPONENTS) REQUIRE_NEW_ALARM_INVENTORY_RESOURCES(alarmWatcher, ALARMS, COMPONENTS)

#define REQUIRE_ALARM_RPC(ALARM_TYPE, RESOURCE, SEVERITY, TEXT) \
    REQUIRE_NEW_ALARM(alarmWatcher, ALARM_TYPE, COMPONENT(RESOURCE), SEVERITY, TEXT)

TEST_CASE("IETF Hardware with sysrepo")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;

    srSess.sendRPC(srSess.getContext().newPath("/ietf-factory-default:factory-reset"));

    auto alarmsClient = sysrepo::Connection{}.sessionStart(sysrepo::Datastore::Operational);

    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    client.switchDatastore(sysrepo::Datastore::Operational);

    trompeloeil::sequence seq1;

    auto directLeafNodeQuery = [&](const std::string& xpath) {
        auto val = client.getData(xpath);
        REQUIRE(val);
        return velia::utils::asString(*val->findPath(xpath));
    };

    auto sysfsTempCpu = std::make_shared<FakeHWMon>();
    auto sysfsPower = std::make_shared<FakeHWMon>();

    using velia::ietf_hardware::OneThreshold;
    using velia::ietf_hardware::Thresholds;
    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;

    std::atomic<bool> psuActive; // this needs to be destroyed after ietfHardware to avoid dangling reference (we are passing it as a ref to PsuDataReader)
    std::atomic<int64_t> psuSensorValue;
    std::atomic<int64_t> cpuTempValue;
    std::atomic<int64_t> powerValue;

    // register components into hw state
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();
    ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:temperature-cpu", "ne", sysfsTempCpu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>(
        "ne:power",
        "ne",
        sysfsPower,
        1,
        Thresholds<int64_t>{
            .criticalLow = OneThreshold<int64_t>{8'000'000, 500'000},
            .warningLow = OneThreshold<int64_t>{10'000'000, 500'000},
            .warningHigh = OneThreshold<int64_t>{20'000'000, 500'000},
            .criticalHigh = OneThreshold<int64_t>{22'000'000, 500'000},
        }));

    /* Some data readers (like our PSU reader, see the FspYhPsu test) may set oper-state to enabled/disabled depending on whether the device is present and Some
     * data might not even be pushed (e.g. the child sensors).
     * Since we push data into sysrepo we have to erase old data (that should no longer be present) from the sysrepo operational DS.
     * We test such situation via the following data reader which returns data only when psuActive is set to true.
     */
    struct PsuDataReader {
        const std::atomic<bool>& active;
        const std::atomic<int64_t>& value;

        velia::ietf_hardware::SensorPollData operator()()
        {
            velia::ietf_hardware::SideLoadedAlarm alarm;
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
                res[COMPONENT("ne:psu:child") "/sensor-data/value"] = std::to_string(value);
                res[COMPONENT("ne:psu:child") "/sensor-data/value-precision"] = "0";
                res[COMPONENT("ne:psu:child") "/sensor-data/value-scale"] = "milli";
                res[COMPONENT("ne:psu:child") "/sensor-data/value-type"] = "volts-DC";

                thr[COMPONENT("ne:psu:child") "/sensor-data/value"] = Thresholds<int64_t>{
                    .criticalLow = std::nullopt,
                    .warningLow = OneThreshold<int64_t>{10000, 2000},
                    .warningHigh = OneThreshold<int64_t>{15000, 2000},
                    .criticalHigh = std::nullopt,
                };

                alarm = {"velia-alarms:sensor-missing-alarm", COMPONENT("ne:psu"), "cleared", "PSU missing."};
            } else {
                alarm = {"velia-alarms:sensor-missing-alarm", COMPONENT("ne:psu"), "critical", "PSU missing."};
            }

            return {res, thr, {alarm}};
        }
    };
    ietfHardware->registerDataReader(PsuDataReader{psuActive, psuSensorValue});

    /* Ensure that there are sane data after each sysrepo change callback (all the component subtrees are expected). */
    DatastoreWatcher dsChangeHardware(client, "/ietf-hardware:hardware/component", {"/ietf-hardware:hardware/last-change"});
    AlarmWatcher alarmWatcher(client);

    SECTION("Disappearing sensor plugged from the beginning")
    {
        // first batch of values
        cpuTempValue = 41800;
        powerValue = 0;
        psuActive = true;
        psuSensorValue = 12000;
        REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
        REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));

        REQUIRE_ALARM_INVENTORY_ADD_ALARMS(
            INTRODUCED_ALARM("velia-alarms:sensor-low-value-alarm", "Sensor value is below the low threshold."),
            INTRODUCED_ALARM("velia-alarms:sensor-high-value-alarm", "Sensor value is above the high threshold."),
            INTRODUCED_ALARM("velia-alarms:sensor-missing-alarm", "Sensor is missing."),
            INTRODUCED_ALARM("velia-alarms:sensor-nonoperational", "Sensor is flagged as nonoperational."))
            .IN_SEQUENCE(seq1);

        REQUIRE_ALARM_INVENTORY_ADD_RESOURCES(
            ALARMS("velia-alarms:sensor-low-value-alarm",
                   "velia-alarms:sensor-high-value-alarm",
                   "velia-alarms:sensor-missing-alarm",
                   "velia-alarms:sensor-nonoperational"),
            COMPONENTS(
                COMPONENT("ne:power"),
                COMPONENT("ne:psu:child"),
                COMPONENT("ne:temperature-cpu")))
            .IN_SEQUENCE(seq1);

        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne") "/class", "iana-hardware:chassis"},
                                           {COMPONENT("ne") "/mfg-name", "CESNET"},
                                           {COMPONENT("ne") "/name", "ne"},
                                           {COMPONENT("ne") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:power") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:power") "/name", "ne:power"},
                                           {COMPONENT("ne:power") "/parent", "ne"},
                                           {COMPONENT("ne:power") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:power") "/sensor-data/value", "0"},
                                           {COMPONENT("ne:power") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:power") "/sensor-data/value-scale", "micro"},
                                           {COMPONENT("ne:power") "/sensor-data/value-type", "watts"},
                                           {COMPONENT("ne:power") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:psu") "/class", "iana-hardware:power-supply"},
                                           {COMPONENT("ne:psu") "/name", "ne:psu"},
                                           {COMPONENT("ne:psu") "/parent", "ne"},
                                           {COMPONENT("ne:psu") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:psu:child") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:psu:child") "/name", "ne:psu:child"},
                                           {COMPONENT("ne:psu:child") "/parent", "ne:psu"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value", "12000"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-scale", "milli"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-type", "volts-DC"},
                                           {COMPONENT("ne:psu:child") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:temperature-cpu") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:temperature-cpu") "/name", "ne:temperature-cpu"},
                                           {COMPONENT("ne:temperature-cpu") "/parent", "ne"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value", "41800"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value-scale", "milli"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value-type", "celsius"},
                                           {COMPONENT("ne:temperature-cpu") "/state/oper-state", "enabled"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_INVENTORY_ADD_RESOURCES(ALARMS("velia-alarms:sensor-missing-alarm"), COMPONENTS(COMPONENT("ne:psu"))).TIMES(1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-low-value-alarm", "ne:power", "critical", "Sensor value crossed low threshold (0 < 8000000).").IN_SEQUENCE(seq1);

        auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSess, ietfHardware, 150ms);
        std::this_thread::sleep_for(400ms); // let's wait until the bg polling thread is spawned; 400 ms is probably enough to spawn the thread and poll 2 or 3 times

        std::string lastChange = directLeafNodeQuery(modulePrefix + "/last-change");

        // second batch of values, sensor data changed, PSU ejected
        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:psu:child") "/class", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/parent", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/oper-status", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-precision", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-scale", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-type", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/state/oper-state", Deleted{}},
                                           {COMPONENT("ne:power") "/sensor-data/value", "11222333"},
                                           {COMPONENT("ne:psu") "/state/oper-state", "disabled"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value", "222"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu", "critical", "PSU missing.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-low-value-alarm", "ne:power", "cleared", "Sensor value is within normal parameters.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu:child", "warning",
                "Sensor value not reported. Maybe the sensor was unplugged?").IN_SEQUENCE(seq1);
        REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
        REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));
        cpuTempValue = 222;
        powerValue = 11222333;
        psuActive = false;

        std::this_thread::sleep_for(2000ms); // longer sleep here: last-change does not report milliseconds so this should increase last-change timestamp at least by one second
        REQUIRE(directLeafNodeQuery(modulePrefix + "/last-change") > lastChange); // check that last-change leaf has timestamp that is greater than the previous one

        // third batch of changes, wild PSU appears with a warning
        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:psu") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:psu:child") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:psu:child") "/parent", "ne:psu"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value", "50000"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-scale", "milli"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-type", "volts-DC"},
                                           {COMPONENT("ne:psu:child") "/state/oper-state", "enabled"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu", "cleared", "PSU missing.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu:child", "cleared", "Sensor value not reported. Maybe the sensor was unplugged?").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-high-value-alarm", "ne:psu:child", "warning", "Sensor value crossed high threshold (50000 > 15000).").IN_SEQUENCE(seq1);
        psuSensorValue = 50000;
        psuActive = true;

        waitForCompletionAndBitMore(seq1);

        // fourth round. We unplug with a warning
        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:psu:child") "/class", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/parent", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/oper-status", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-precision", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-scale", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-type", Deleted{}},
                                           {COMPONENT("ne:psu:child") "/state/oper-state", Deleted{}},
                                           {COMPONENT("ne:psu") "/state/oper-state", "disabled"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu", "critical", "PSU missing.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu:child", "warning", "Sensor value not reported. Maybe the sensor was unplugged?").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-high-value-alarm", "ne:psu:child", "cleared", "Sensor value is within normal parameters.").IN_SEQUENCE(seq1);
        psuActive = false;
        waitForCompletionAndBitMore(seq1);

        // 5+th round: test threshold crossings
        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:power") "/sensor-data/value", "21000000"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-high-value-alarm", "ne:power", "warning", "Sensor value crossed high threshold (21000000 > 20000000).").IN_SEQUENCE(seq1);
        powerValue = 21'000'000;
        waitForCompletionAndBitMore(seq1);

        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:power") "/sensor-data/value", "24000000"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-high-value-alarm", "ne:power", "critical", "Sensor value crossed high threshold (24000000 > 22000000).").IN_SEQUENCE(seq1);
        powerValue = 24'000'000;
        waitForCompletionAndBitMore(seq1);

        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:power") "/sensor-data/value", "1"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-low-value-alarm", "ne:power", "critical", "Sensor value crossed low threshold (1 < 8000000).").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-high-value-alarm", "ne:power", "cleared", "Sensor value is within normal parameters.").IN_SEQUENCE(seq1);
        powerValue = 1;
        waitForCompletionAndBitMore(seq1);

        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:power") "/sensor-data/value", "14000000"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-low-value-alarm", "ne:power", "cleared", "Sensor value is within normal parameters.").IN_SEQUENCE(seq1);
        powerValue = 14'000'000;
        waitForCompletionAndBitMore(seq1);


        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:power") "/sensor-data/value", "1000000000"},
                                           {COMPONENT("ne:power") "/sensor-data/oper-status", "nonoperational"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-nonoperational", "ne:power", "warning",
                "Sensor is nonoperational. The values it reports may not be relevant.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-high-value-alarm", "ne:power", "critical",
                "Sensor value crossed high threshold (1000000000 > 22000000).").IN_SEQUENCE(seq1);
        powerValue = 2'999'999'999;
        waitForCompletionAndBitMore(seq1);

        powerValue = 1'999'999'999;
        waitForCompletionAndBitMore(seq1);

        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:power") "/sensor-data/value", "-1000000000"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-low-value-alarm", "ne:power", "critical", "Sensor value crossed low threshold (-1000000000 < 8000000).").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-high-value-alarm", "ne:power", "cleared", "Sensor value is within normal parameters.").IN_SEQUENCE(seq1);
        powerValue = -2'999'999'999;
        waitForCompletionAndBitMore(seq1);

        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:power") "/sensor-data/value", "-999999999"},
                                           {COMPONENT("ne:power") "/sensor-data/oper-status", "ok"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-nonoperational", "ne:power", "cleared",
                "Sensor is nonoperational. The values it reports may not be relevant.").IN_SEQUENCE(seq1);
        powerValue = -999'999'999;
        waitForCompletionAndBitMore(seq1);
    }

    SECTION("Disappearing sensor unplugged in the beginning")
    {
        cpuTempValue = 41800;
        powerValue = 0;
        psuActive = false;
        psuSensorValue = 12000;
        REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
        REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));
        REQUIRE_ALARM_INVENTORY_ADD_ALARMS(
            INTRODUCED_ALARM("velia-alarms:sensor-low-value-alarm", "Sensor value is below the low threshold."),
            INTRODUCED_ALARM("velia-alarms:sensor-high-value-alarm", "Sensor value is above the high threshold."),
            INTRODUCED_ALARM("velia-alarms:sensor-missing-alarm", "Sensor is missing."),
            INTRODUCED_ALARM("velia-alarms:sensor-nonoperational", "Sensor is flagged as nonoperational."))
            .IN_SEQUENCE(seq1);

        REQUIRE_ALARM_INVENTORY_ADD_RESOURCES(
            ALARMS("velia-alarms:sensor-low-value-alarm",
                   "velia-alarms:sensor-high-value-alarm",
                   "velia-alarms:sensor-missing-alarm",
                   "velia-alarms:sensor-nonoperational"),
            COMPONENTS(
                COMPONENT("ne:power"),
                COMPONENT("ne:temperature-cpu")))
            .IN_SEQUENCE(seq1);

        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne") "/class", "iana-hardware:chassis"},
                                           {COMPONENT("ne") "/mfg-name", "CESNET"},
                                           {COMPONENT("ne") "/name", "ne"},
                                           {COMPONENT("ne") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:power") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:power") "/name", "ne:power"},
                                           {COMPONENT("ne:power") "/parent", "ne"},
                                           {COMPONENT("ne:power") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:power") "/sensor-data/value", "0"},
                                           {COMPONENT("ne:power") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:power") "/sensor-data/value-scale", "micro"},
                                           {COMPONENT("ne:power") "/sensor-data/value-type", "watts"},
                                           {COMPONENT("ne:power") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:psu") "/class", "iana-hardware:power-supply"},
                                           {COMPONENT("ne:psu") "/name", "ne:psu"},
                                           {COMPONENT("ne:psu") "/parent", "ne"},
                                           {COMPONENT("ne:psu") "/state/oper-state", "disabled"},
                                           {COMPONENT("ne:temperature-cpu") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:temperature-cpu") "/name", "ne:temperature-cpu"},
                                           {COMPONENT("ne:temperature-cpu") "/parent", "ne"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value", "41800"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value-scale", "milli"},
                                           {COMPONENT("ne:temperature-cpu") "/sensor-data/value-type", "celsius"},
                                           {COMPONENT("ne:temperature-cpu") "/state/oper-state", "enabled"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_INVENTORY_ADD_RESOURCES(ALARMS("velia-alarms:sensor-missing-alarm"), COMPONENTS(COMPONENT("ne:psu"))).TIMES(AT_LEAST(1));
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu", "critical", "PSU missing.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-low-value-alarm", "ne:power", "critical", "Sensor value crossed low threshold (0 < 8000000).").IN_SEQUENCE(seq1);

        auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSess, ietfHardware, 150ms);
        std::this_thread::sleep_for(400ms); // let's wait until the bg polling thread is spawned; 400 ms is probably enough to spawn the thread and poll 2 or 3 times
        waitForCompletionAndBitMore(seq1);

        std::string lastChange = directLeafNodeQuery(modulePrefix + "/last-change");

        // PSU inserted
        REQUIRE_ALARM_INVENTORY_ADD_RESOURCES(
            ALARMS("velia-alarms:sensor-low-value-alarm",
                   "velia-alarms:sensor-high-value-alarm",
                   "velia-alarms:sensor-missing-alarm",
                   "velia-alarms:sensor-nonoperational"),
            COMPONENTS(COMPONENT("ne:psu:child")))
            .IN_SEQUENCE(seq1);
        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:psu") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:psu:child") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:psu:child") "/name", "ne:psu:child"},
                                           {COMPONENT("ne:psu:child") "/parent", "ne:psu"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value", "12000"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-scale", "milli"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-type", "volts-DC"},
                                           {COMPONENT("ne:psu:child") "/state/oper-state", "enabled"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu", "cleared", "PSU missing.").IN_SEQUENCE(seq1);
        psuActive = true;
        waitForCompletionAndBitMore(seq1);

        std::this_thread::sleep_for(1000ms); // last-change leaf resolution is in seconds, let's wait until the second increments
        REQUIRE(directLeafNodeQuery(modulePrefix + "/last-change") > lastChange); // check that last-change leaf has timestamp that is greater than the previous one

        // unplug again
        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                                       {COMPONENT("ne:psu") "/state/oper-state", "disabled"},
                                                       {COMPONENT("ne:psu:child") "/class", Deleted{}},
                                                       {COMPONENT("ne:psu:child") "/parent", Deleted{}},
                                                       {COMPONENT("ne:psu:child") "/sensor-data/oper-status", Deleted{}},
                                                       {COMPONENT("ne:psu:child") "/sensor-data/value", Deleted{}},
                                                       {COMPONENT("ne:psu:child") "/sensor-data/value-precision", Deleted{}},
                                                       {COMPONENT("ne:psu:child") "/sensor-data/value-scale", Deleted{}},
                                                       {COMPONENT("ne:psu:child") "/sensor-data/value-type", Deleted{}},
                                                       {COMPONENT("ne:psu:child") "/state/oper-state", Deleted{}},
                                                   }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu", "critical", "PSU missing.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu:child", "warning",
                "Sensor value not reported. Maybe the sensor was unplugged?").IN_SEQUENCE(seq1);
        psuActive = false;
        waitForCompletionAndBitMore(seq1);


        // PSU inserted again - no inventory-alarm updates expected
        REQUIRE_DATASTORE_CHANGE(dsChangeHardware, (ValueChanges{
                                           {COMPONENT("ne:psu") "/state/oper-state", "enabled"},
                                           {COMPONENT("ne:psu:child") "/class", "iana-hardware:sensor"},
                                           {COMPONENT("ne:psu:child") "/parent", "ne:psu"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/oper-status", "ok"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value", "12000"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-precision", "0"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-scale", "milli"},
                                           {COMPONENT("ne:psu:child") "/sensor-data/value-type", "volts-DC"},
                                           {COMPONENT("ne:psu:child") "/state/oper-state", "enabled"},
                                       }))
            .IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu", "cleared", "PSU missing.").IN_SEQUENCE(seq1);
        REQUIRE_ALARM_RPC("velia-alarms:sensor-missing-alarm", "ne:psu:child", "cleared",
                "Sensor value not reported. Maybe the sensor was unplugged?").IN_SEQUENCE(seq1);
        psuActive = true;
        waitForCompletionAndBitMore(seq1);
    }
}
