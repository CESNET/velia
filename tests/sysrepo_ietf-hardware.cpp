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

TEST_CASE("IETF Hardware with sysrepo")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    client.switchDatastore(sysrepo::Datastore::Operational);

    DatastoreChange dsChange;

    trompeloeil::sequence seq1;

    auto directLeafNodeQuery = [&](const std::string& xpath) {
        auto val = client.getData(xpath);
        REQUIRE(val);
        return std::string{val->findPath(xpath)->asTerm().valueStr()};
    };

    auto sysfsTempCpu = std::make_shared<FakeHWMon>();
    auto sysfsPower = std::make_shared<FakeHWMon>();

    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;

    std::atomic<bool> psuActive; // this needs to be destroyed after ietfHardware to avoid dangling reference (we are passing it as a ref to PsuDataReader)
    std::atomic<int64_t> cpuTempValue;
    std::atomic<int64_t> powerValue;

    // register components into hw state
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();
    ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:temperature-cpu", "ne", sysfsTempCpu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:power", "ne", sysfsPower, 1));

    /* Some data readers (like our PSU reader, see the FspYhPsu test) may set oper-state to enabled/disabled depending on whether the device is present and Some
     * data might not even be pushed (e.g. the child sensors).
     * Since we push data into sysrepo we have to erase old data (that should no longer be present) from the sysrepo operational DS.
     * We test such situation via the following data reader which returns data only when psuActive is set to true.
     */
    struct PsuDataReader {
        const std::atomic<bool>& active;

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
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value"] = "12000";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-precision"] = "0";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-scale"] = "milli";
                res["/ietf-hardware:hardware/component[name='ne:psu:child']/sensor-data/value-type"] = "volts-DC";
            }

            return res;
        }
    };
    ietfHardware->registerDataReader(PsuDataReader{psuActive});

    /* Ensure that there are sane data after each sysrepo change callback (all the component subtrees are expected). */
    auto changeSub = client.onModuleChange(
        "ietf-hardware",
        [&](sysrepo::Session session, auto, auto, auto, auto, auto) {
            std::map<std::string, std::variant<std::string, Deleted>> changes;

            for (const auto& change : session.getChanges()) {
                if (change.node.path() == "/ietf-hardware:hardware/last-change") { // skip timestamp changes - we can't test them properly in expectations with current code
                    continue;
                }

                if (change.operation == sysrepo::ChangeOperation::Deleted) {
                    changes.emplace(change.node.path(), Deleted());
                } else {
                    changes.emplace(change.node.path(), nodeAsString(change.node));
                }
            }

            dsChange.change(changes);
            return sysrepo::ErrorCode::Ok;
        },
        "/ietf-hardware:hardware/component",
        0,
        sysrepo::SubscribeOptions::DoneOnly);

    // first batch of values
    cpuTempValue = 41800;
    powerValue = 14000000;
    psuActive = true;
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(dsChange, change(std::map<std::string, std::variant<std::string, Deleted>>{
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
    REQUIRE_CALL(dsChange, change(std::map<std::string, std::variant<std::string, Deleted>>{
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
    REQUIRE_CALL(dsChange, change(std::map<std::string, std::variant<std::string, Deleted>>{
                               {"/ietf-hardware:hardware/component[name='ne:power']/sensor-data/value", "11222333"},
                               {"/ietf-hardware:hardware/component[name='ne:psu']/state/oper-state", "disabled"},
                               {"/ietf-hardware:hardware/component[name='ne:temperature-cpu']/sensor-data/value", "222"},
                           }))
        .IN_SEQUENCE(seq1);
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));
    cpuTempValue = 222;
    powerValue = 11222333;
    psuActive = false;

    std::this_thread::sleep_for(2000ms); // longer sleep here: last-change does not report milliseconds so this should increase last-change timestamp at least by one second
    REQUIRE(directLeafNodeQuery(modulePrefix + "/last-change") > lastChange); // check that last-change leaf has timestamp that is greater than the previous one

    // third batch of changes, wild PSU appears
    REQUIRE_CALL(dsChange, change(std::map<std::string, std::variant<std::string, Deleted>>{
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
                           }))
        .IN_SEQUENCE(seq1);
    psuActive = true;

    waitForCompletionAndBitMore(seq1);
}
