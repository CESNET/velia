#include "trompeloeil_doctest.h"
#include <iterator>
#include <sysrepo-cpp/Enum.hpp>
#include <trompeloeil.hpp>
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

using namespace std::literals;

std::string nodeAsString(const libyang::DataNode& node)
{
    switch (node.schema().nodeType()) {
    case libyang::NodeType::Container:
        return "(container)";
    case libyang::NodeType::List:
        return "(list instance)";
    case libyang::NodeType::Leaf:
    case libyang::NodeType::Leaflist:
        return std::string(node.asTerm().valueStr());
    default:
        return "(unprintable)";
    }
};

struct Deleted { };
bool operator==(const Deleted&, const Deleted&) { return true; }

namespace trompeloeil {
template <>
struct printer<std::map<std::string, std::variant<std::string, Deleted>>> {
    static void print(std::ostream& os, const std::map<std::string, std::variant<std::string, Deleted>>& map)
    {
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << key << "\": \""
               << std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, Deleted>)
                    return "Deleted()";
                if constexpr (std::is_same_v<T, std::string>)
                    return arg; }, value)
               << "\"," << std::endl;
        }
        os << "}";
    }
};
}

struct DatastoreChange {
    MAKE_CONST_MOCK1(change, void(const std::map<std::string, std::variant<std::string, Deleted>>&));
};

struct AlarmEvent {
    MAKE_CONST_MOCK1(event, void(const std::map<std::string, std::string>&));
};

void processDsChanges(sysrepo::Session session, DatastoreChange& dsChange, const std::set<std::string>& ignoredPaths)
{
    std::map<std::string, std::variant<std::string, Deleted>> changes;

    for (const auto& change : session.getChanges()) {
        if (ignoredPaths.contains(change.node.schema().path())) {
            continue;
        }

        if (change.operation == sysrepo::ChangeOperation::Deleted) {
            changes.emplace(change.node.path(), Deleted());
        } else {
            changes.emplace(change.node.path(), nodeAsString(change.node));
        }
    }

    dsChange.change(changes);
}

TEST_CASE("IETF Hardware with sysrepo")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    auto alarmsClient = sysrepo::Connection{}.sessionStart(sysrepo::Datastore::Operational);

    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    client.switchDatastore(sysrepo::Datastore::Operational);

    DatastoreChange dsChangeHardware;
    DatastoreChange dsChangeAlarmInventory;
    AlarmEvent alarmEvents;

    trompeloeil::sequence seq1;

    auto alarmsRPC = alarmsClient.onRPCAction("/sysrepo-ietf-alarms:create-or-update-alarm", [&](auto, auto, auto, const libyang::DataNode input, auto, auto, auto) {
        std::map<std::string, std::string> inputData;

        for (const auto& node : input.childrenDfs()) {
            inputData.emplace(node.path(), nodeAsString(node));
        }

        alarmEvents.event(inputData);

        return sysrepo::ErrorCode::Ok;
    });

    auto directLeafNodeQuery = [&](const std::string& xpath) {
        auto val = client.getData(xpath);
        REQUIRE(val);
        return std::string{val->findPath(xpath)->asTerm().valueStr()};
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
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:power", "ne", sysfsPower, 1, Thresholds<int64_t>{
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
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value"] = std::to_string(value);
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
    ietfHardware->registerDataReader(PsuDataReader{psuActive, psuSensorValue});

    /* Ensure that there are sane data after each sysrepo change callback (all the component subtrees are expected). */
    auto changeSub = client.onModuleChange(
        "ietf-hardware",
        [&](sysrepo::Session session, auto, auto, auto, auto, auto) {
            processDsChanges(session, dsChangeHardware, {"/ietf-hardware:hardware/last-change"});
            return sysrepo::ErrorCode::Ok;
        },
        "/ietf-hardware:hardware/component",
        0,
        sysrepo::SubscribeOptions::DoneOnly);

    auto alarmsInvSub = alarmsClient.onModuleChange(
        "ietf-alarms",
        [&](sysrepo::Session session, auto, auto, auto, auto, auto) {
            processDsChanges(session, dsChangeAlarmInventory, {});
            return sysrepo::ErrorCode::Ok;
        },
        "/ietf-alarms:alarms/alarm-inventory",
        0,
        sysrepo::SubscribeOptions::DoneOnly);

    // first batch of values
    cpuTempValue = 41800;
    powerValue = 14000000;
    psuActive = true;
    psuSensorValue = 12000;
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(dsChangeAlarmInventory, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                             {"/ietf-alarms:alarms", "(container)"},
                                             {"/ietf-alarms:alarms/alarm-inventory", "(container)"},
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:threshold-crossing-alarm'][alarm-type-qualifier='']", "(list instance)"},
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:threshold-crossing-alarm'][alarm-type-qualifier='']/alarm-type-id", "velia-alarms:threshold-crossing-alarm"},
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:threshold-crossing-alarm'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:threshold-crossing-alarm'][alarm-type-qualifier='']/resource[1]", "/ietf-hardware:hardware/component[name='ne:power']"},
                                         }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(dsChangeAlarmInventory, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:sensor-missing'][alarm-type-qualifier='']", "(list instance)"},
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:sensor-missing'][alarm-type-qualifier='']/alarm-type-id", "velia-alarms:sensor-missing"},
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:sensor-missing'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:sensor-missing'][alarm-type-qualifier='']/resource[1]", "/ietf-hardware:hardware/component[name='ne:power']"},
                                         }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(dsChangeAlarmInventory, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:threshold-crossing-alarm'][alarm-type-qualifier='']/resource[1]", "/ietf-hardware:hardware/component[name='ne:psu:child']"},
                                         }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(dsChangeAlarmInventory, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:sensor-missing'][alarm-type-qualifier='']/resource[1]", "/ietf-hardware:hardware/component[name='ne:psu:child']"},
                                         }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(dsChangeAlarmInventory, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:threshold-crossing-alarm'][alarm-type-qualifier='']/resource[1]", "/ietf-hardware:hardware/component[name='ne:temperature-cpu']"},
                                         }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(dsChangeAlarmInventory, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                             {"/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='velia-alarms:sensor-missing'][alarm-type-qualifier='']/resource[1]", "/ietf-hardware:hardware/component[name='ne:temperature-cpu']"},
                                         }))
        .IN_SEQUENCE(seq1);

    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne']", "(list instance)"},
                                       {"/ietf-hardware:hardware/component[name='ne']/class", "iana-hardware:chassis"},
                                       {"/ietf-hardware:hardware/component[name='ne']/mfg-name", "CESNET"},
                                       {"/ietf-hardware:hardware/component[name='ne']/name", "ne"},
                                       {"/ietf-hardware:hardware/component[name='ne']/state", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne']/state/oper-state", "enabled"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']", "(list instance)"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/class", "iana-hardware:sensor"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/name", "ne:power"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/parent", "ne"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/oper-status", "ok"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value", "14000000"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value-precision", "0"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value-scale", "micro"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value-type", "watts"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/state", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:power']/state/oper-state", "enabled"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu']", "(list instance)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/class", "iana-hardware:power-supply"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/name", "ne:psu"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/parent", "ne"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/state", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "enabled"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']", "(list instance)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/class", "iana-hardware:sensor"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/name", "ne:psu:child"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/parent", "ne:psu"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status", "ok"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value", "12000"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision", "0"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale", "milli"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type", "volts-DC"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state", "enabled"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']", "(list instance)"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/class", "iana-hardware:sensor"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/name", "ne:temperature-cpu"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/parent", "ne"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data/oper-status", "ok"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data/value", "41800"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data/value-precision", "0"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data/value-scale", "milli"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data/value-type", "celsius"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/state", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/state/oper-state", "enabled"},
                                   }))
        .IN_SEQUENCE(seq1);

    auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSess, ietfHardware, 150ms);
    std::this_thread::sleep_for(400ms); // let's wait until the bg polling thread is spawned; 400 ms is probably enough to spawn the thread and poll 2 or 3 times

    std::string lastChange = directLeafNodeQuery(modulePrefix + "/last-change");

    // second batch of values, sensor data changed, PSU ejected
    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/class", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/name", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/parent", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state", Deleted{}},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value", "11222333"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "disabled"},
                                       {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data/value", "222"},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor is missing. Maybe it was unplugged?"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:sensor-missing"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:psu:child']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "critical"},
                              }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));
    cpuTempValue = 222;
    powerValue = 11222333;
    psuActive = false;

    std::this_thread::sleep_for(2000ms); // longer sleep here: last-change does not report milliseconds so this should increase last-change timestamp at least by one second
    REQUIRE(directLeafNodeQuery(modulePrefix + "/last-change") > lastChange); // check that last-change leaf has timestamp that is greater than the previous one

    // third batch of changes, wild PSU appears with a warning
    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "enabled"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']", "(list instance)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/class", "iana-hardware:sensor"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/name", "ne:psu:child"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/parent", "ne:psu"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status", "ok"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value", "50000"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision", "0"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale", "milli"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type", "volts-DC"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state", "(container)"},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state", "enabled"},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor is missing. Maybe it was unplugged?"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:sensor-missing"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:psu:child']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "cleared"},
                              }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor threshold crossed."},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:threshold-crossing-alarm"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:psu:child']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "warning"},
                              }))
        .IN_SEQUENCE(seq1);
    psuSensorValue = 50000;
    psuActive = true;

    waitForCompletionAndBitMore(seq1);

    // fourth round. We unplug with a warning
    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/class", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/name", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/parent", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/oper-status", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state", Deleted{}},
                                       {"/ietf-hardware:hardware/component[name='ne:psu:child']/state/oper-state", Deleted{}},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "disabled"},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor is missing. Maybe it was unplugged?"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:sensor-missing"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:psu:child']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "critical"},
                              }))
        .IN_SEQUENCE(seq1);
    psuActive = false;
    waitForCompletionAndBitMore(seq1);

    // 5+th round: test threshold crossings
    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value", "21000000"},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor threshold crossed."},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:threshold-crossing-alarm"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:power']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "warning"},
                              }))
        .IN_SEQUENCE(seq1);
    powerValue = 21'000'000;
    waitForCompletionAndBitMore(seq1);

    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value", "24000000"},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor threshold crossed."},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:threshold-crossing-alarm"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:power']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "critical"},
                              }))
        .IN_SEQUENCE(seq1);
    powerValue = 24'000'000;
    waitForCompletionAndBitMore(seq1);

    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value", "1"},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor threshold crossed."},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:threshold-crossing-alarm"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:power']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "critical"},
                              }))
        .IN_SEQUENCE(seq1);
    powerValue = 1;
    waitForCompletionAndBitMore(seq1);

    REQUIRE_CALL(dsChangeHardware, change(std::map<std::string, std::variant<std::string, Deleted>>{
                                       {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value", "14000000"},
                                   }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(alarmEvents, event(std::map<std::string, std::string>{
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm", "(unprintable)"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-text", "Sensor threshold crossed."},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-id", "velia-alarms:threshold-crossing-alarm"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/alarm-type-qualifier", ""},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/resource", "/ietf-hardware:hardware/component[name='ne:power']"},
                                  {"/sysrepo-ietf-alarms:create-or-update-alarm/severity", "cleared"},
                              }))
        .IN_SEQUENCE(seq1);
    powerValue = 14'000'000;
    waitForCompletionAndBitMore(seq1);
}
