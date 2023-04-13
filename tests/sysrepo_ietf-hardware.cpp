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

struct ModuleChangeTester {
    MAKE_CONST_MOCK1(components, void(const std::set<std::string>&));
};

TEST_CASE("IETF Hardware with sysrepo")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    client.switchDatastore(sysrepo::Datastore::Operational);

    ModuleChangeTester modChangeTester;

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
            }

            return res;
        }
    };
    ietfHardware->registerDataReader(PsuDataReader{psuActive});

    /* Ensure that there are sane data after each sysrepo change callback (all the component subtrees are expected). */
    auto changeSub = client.onModuleChange(
        "ietf-hardware",
        [&](sysrepo::Session session, auto, auto, auto, auto, auto) {
            std::set<std::string> componentXPaths;

            if (auto data = session.getData("/ietf-hardware:hardware/component")) {
                for (const auto& e : data->findXPath("/ietf-hardware:hardware/component")) {
                    componentXPaths.emplace(e.path());
                }
            }

            modChangeTester.components(componentXPaths);
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
    REQUIRE_CALL(modChangeTester, components(std::set<std::string>{
                                      "/ietf-hardware:hardware/component[name='ne']",
                                      "/ietf-hardware:hardware/component[name='ne:power']",
                                      "/ietf-hardware:hardware/component[name='ne:temperature-cpu']",
                                      "/ietf-hardware:hardware/component[name='ne:psu']",
                                      "/ietf-hardware:hardware/component[name='ne:psu:child']",
                                  })).TIMES(1 /* addition of new components does not trigger any delete */).IN_SEQUENCE(seq1);

    auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSess, ietfHardware, 150ms);
    std::this_thread::sleep_for(400ms); // let's wait until the bg polling thread is spawned; 400 ms is probably enough to spawn the thread and poll 2 or 3 times

    std::string lastChange = directLeafNodeQuery(modulePrefix + "/last-change");
    REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/class") == "iana-hardware:sensor"s);
    REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/sensor-data/value") == std::to_string(powerValue));
    REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                {"[name='ne']", ""},
                {"[name='ne']/name", "ne"},
                {"[name='ne']/class", "iana-hardware:chassis"},
                {"[name='ne']/mfg-name", "CESNET"},
                {"[name='ne']/state", ""},
                {"[name='ne']/state/oper-state", "enabled"},

                {"[name='ne:psu']", ""},
                {"[name='ne:psu']/name", "ne:psu"},
                {"[name='ne:psu']/class", "iana-hardware:power-supply"},
                {"[name='ne:psu']/parent", "ne"},
                {"[name='ne:psu']/state", ""},
                {"[name='ne:psu']/state/oper-state", "enabled"},
                {"[name='ne:psu:child']", ""},
                {"[name='ne:psu:child']/name", "ne:psu:child"},
                {"[name='ne:psu:child']/class", "iana-hardware:sensor"},
                {"[name='ne:psu:child']/parent", "ne:psu"},
                {"[name='ne:psu:child']/state", ""},
                {"[name='ne:psu:child']/state/oper-state", "enabled"},
                {"[name='ne:psu:child']/sensor-data", ""},

                {"[name='ne:temperature-cpu']", ""},
                {"[name='ne:temperature-cpu']/name", "ne:temperature-cpu"},
                {"[name='ne:temperature-cpu']/class", "iana-hardware:sensor"},
                {"[name='ne:temperature-cpu']/parent", "ne"},
                {"[name='ne:temperature-cpu']/sensor-data", ""},
                {"[name='ne:temperature-cpu']/sensor-data/oper-status", "ok"},
                {"[name='ne:temperature-cpu']/sensor-data/value", "41800"},
                {"[name='ne:temperature-cpu']/sensor-data/value-precision", "0"},
                {"[name='ne:temperature-cpu']/sensor-data/value-scale", "milli"},
                {"[name='ne:temperature-cpu']/sensor-data/value-type", "celsius"},
                {"[name='ne:temperature-cpu']/state", ""},
                {"[name='ne:temperature-cpu']/state/oper-state", "enabled"},

                {"[name='ne:power']", ""},
                {"[name='ne:power']/name", "ne:power"},
                {"[name='ne:power']/class", "iana-hardware:sensor"},
                {"[name='ne:power']/parent", "ne"},
                {"[name='ne:power']/sensor-data", ""},
                {"[name='ne:power']/sensor-data/oper-status", "ok"},
                {"[name='ne:power']/sensor-data/value", "14000000"},
                {"[name='ne:power']/sensor-data/value-precision", "0"},
                {"[name='ne:power']/sensor-data/value-scale", "micro"},
                {"[name='ne:power']/sensor-data/value-type", "watts"},
                {"[name='ne:power']/state", ""},
                {"[name='ne:power']/state/oper-state", "enabled"},
            });

    // second batch of values, sensor data changed, PSU ejected
    REQUIRE_CALL(modChangeTester, components(std::set<std::string>{
                                      "/ietf-hardware:hardware/component[name='ne']",
                                      "/ietf-hardware:hardware/component[name='ne:power']",
                                      "/ietf-hardware:hardware/component[name='ne:temperature-cpu']",
                                      "/ietf-hardware:hardware/component[name='ne:psu']",
                                  })).TIMES(2 /* one discard call (ne:psu:child), one push with changes to other components */).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));
    cpuTempValue = 222;
    powerValue = 11222333;
    psuActive = false;

    std::this_thread::sleep_for(2000ms); // longer sleep here: last-change does not report milliseconds so this should increase last-change timestamp at least by one second
    REQUIRE(directLeafNodeQuery(modulePrefix + "/last-change") > lastChange); // check that last-change leaf has timestamp that is greater than the previous one
    REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/class") == "iana-hardware:sensor"s);
    REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/sensor-data/value") == std::to_string(powerValue));
    REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                {"[name='ne']", ""},
                {"[name='ne']/name", "ne"},
                {"[name='ne']/class", "iana-hardware:chassis"},
                {"[name='ne']/mfg-name", "CESNET"},
                {"[name='ne']/state", ""},
                {"[name='ne']/state/oper-state", "enabled"},

                {"[name='ne:psu']", ""},
                {"[name='ne:psu']/name", "ne:psu"},
                {"[name='ne:psu']/class", "iana-hardware:power-supply"},
                {"[name='ne:psu']/parent", "ne"},
                {"[name='ne:psu']/state", ""},
                {"[name='ne:psu']/state/oper-state", "disabled"},

                {"[name='ne:temperature-cpu']", ""},
                {"[name='ne:temperature-cpu']/name", "ne:temperature-cpu"},
                {"[name='ne:temperature-cpu']/class", "iana-hardware:sensor"},
                {"[name='ne:temperature-cpu']/parent", "ne"},
                {"[name='ne:temperature-cpu']/sensor-data", ""},
                {"[name='ne:temperature-cpu']/sensor-data/oper-status", "ok"},
                {"[name='ne:temperature-cpu']/sensor-data/value", "222"},
                {"[name='ne:temperature-cpu']/sensor-data/value-precision", "0"},
                {"[name='ne:temperature-cpu']/sensor-data/value-scale", "milli"},
                {"[name='ne:temperature-cpu']/sensor-data/value-type", "celsius"},
                {"[name='ne:temperature-cpu']/state", ""},
                {"[name='ne:temperature-cpu']/state/oper-state", "enabled"},

                {"[name='ne:power']", ""},
                {"[name='ne:power']/name", "ne:power"},
                {"[name='ne:power']/class", "iana-hardware:sensor"},
                {"[name='ne:power']/parent", "ne"},
                {"[name='ne:power']/sensor-data", ""},
                {"[name='ne:power']/sensor-data/oper-status", "ok"},
                {"[name='ne:power']/sensor-data/value", "11222333"},
                {"[name='ne:power']/sensor-data/value-precision", "0"},
                {"[name='ne:power']/sensor-data/value-scale", "micro"},
                {"[name='ne:power']/sensor-data/value-type", "watts"},
                {"[name='ne:power']/state", ""},
                {"[name='ne:power']/state/oper-state", "enabled"},
            });

    // third batch of changes, wild PSU appears
    REQUIRE_CALL(modChangeTester, components(std::set<std::string>{
                                      "/ietf-hardware:hardware/component[name='ne']",
                                      "/ietf-hardware:hardware/component[name='ne:power']",
                                      "/ietf-hardware:hardware/component[name='ne:temperature-cpu']",
                                      "/ietf-hardware:hardware/component[name='ne:psu']",
                                      "/ietf-hardware:hardware/component[name='ne:psu:child']",
                                  })).TIMES(1 /* addition of a new component does not trigger any delete */).IN_SEQUENCE(seq1);
    psuActive = true;

    std::this_thread::sleep_for(400ms);
    REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                {"[name='ne']", ""},
                {"[name='ne']/name", "ne"},
                {"[name='ne']/class", "iana-hardware:chassis"},
                {"[name='ne']/mfg-name", "CESNET"},
                {"[name='ne']/state", ""},
                {"[name='ne']/state/oper-state", "enabled"},

                {"[name='ne:psu']", ""},
                {"[name='ne:psu']/name", "ne:psu"},
                {"[name='ne:psu']/class", "iana-hardware:power-supply"},
                {"[name='ne:psu']/parent", "ne"},
                {"[name='ne:psu']/state", ""},
                {"[name='ne:psu']/state/oper-state", "enabled"},
                {"[name='ne:psu:child']", ""},
                {"[name='ne:psu:child']/name", "ne:psu:child"},
                {"[name='ne:psu:child']/class", "iana-hardware:sensor"},
                {"[name='ne:psu:child']/parent", "ne:psu"},
                {"[name='ne:psu:child']/state", ""},
                {"[name='ne:psu:child']/state/oper-state", "enabled"},
                {"[name='ne:psu:child']/sensor-data", ""},

                {"[name='ne:temperature-cpu']", ""},
                {"[name='ne:temperature-cpu']/name", "ne:temperature-cpu"},
                {"[name='ne:temperature-cpu']/class", "iana-hardware:sensor"},
                {"[name='ne:temperature-cpu']/parent", "ne"},
                {"[name='ne:temperature-cpu']/sensor-data", ""},
                {"[name='ne:temperature-cpu']/sensor-data/oper-status", "ok"},
                {"[name='ne:temperature-cpu']/sensor-data/value", "222"},
                {"[name='ne:temperature-cpu']/sensor-data/value-precision", "0"},
                {"[name='ne:temperature-cpu']/sensor-data/value-scale", "milli"},
                {"[name='ne:temperature-cpu']/sensor-data/value-type", "celsius"},
                {"[name='ne:temperature-cpu']/state", ""},
                {"[name='ne:temperature-cpu']/state/oper-state", "enabled"},

                {"[name='ne:power']", ""},
                {"[name='ne:power']/name", "ne:power"},
                {"[name='ne:power']/class", "iana-hardware:sensor"},
                {"[name='ne:power']/parent", "ne"},
                {"[name='ne:power']/sensor-data", ""},
                {"[name='ne:power']/sensor-data/oper-status", "ok"},
                {"[name='ne:power']/sensor-data/value", "11222333"},
                {"[name='ne:power']/sensor-data/value-precision", "0"},
                {"[name='ne:power']/sensor-data/value-scale", "micro"},
                {"[name='ne:power']/sensor-data/value-type", "watts"},
                {"[name='ne:power']/state", ""},
                {"[name='ne:power']/state/oper-state", "enabled"},
            });
}

