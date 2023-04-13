#include "trompeloeil_doctest.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "utils/UniqueResource.h"

using namespace std::literals;

TEST_CASE("IETF Hardware with sysrepo")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    trompeloeil::sequence seq1;

    auto directLeafNodeQuery = [&](const std::string& xpath) {
        auto dsSwitch = velia::utils::make_unique_resource([&] { client.switchDatastore(sysrepo::Datastore::Operational); }, [&] { client.switchDatastore(sysrepo::Datastore::Running); });

        auto val = client.getData(xpath);
        REQUIRE(val);
        return val->findPath(xpath)->asTerm().valueStr();
    };

    auto sysfsTempCpu = std::make_shared<FakeHWMon>();
    auto sysfsPower = std::make_shared<FakeHWMon>();

    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;

    // register components into hw state
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();
    ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:temperature-cpu", "ne", sysfsTempCpu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:power", "ne", sysfsPower, 1));

    std::atomic<int64_t> cpuTempValue;
    std::atomic<int64_t> powerValue;

    // first batch of values
    cpuTempValue = 41800;
    powerValue = 14000000;
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));

    auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSess, ietfHardware, 150ms);
    std::this_thread::sleep_for(50ms); // wait for bg thread to spawn

    // at least check that there is some timestamp
    /* std::string lastChange; */
    /* REQUIRE(dataFromSysrepo(client, modulePrefix, sysrepo::Datastore::Operational).count("/last-change") > 0); */

    std::this_thread::sleep_for(400ms); // the expectations should proc several times
    REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/class") == "iana-hardware:sensor"s);
    /* REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/sensor-data/value") == std::to_string(cpuTempValue)); */

    // second batch of values
    cpuTempValue = 222;
    powerValue = 11222333;
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).LR_RETURN(cpuTempValue).TIMES(AT_LEAST(1));
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).LR_RETURN(powerValue).TIMES(AT_LEAST(1));

    std::this_thread::sleep_for(400ms); // the expectations should proc several times
    /* REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/class") == "iana-hardware:sensor"s); */
    /* REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/sensor-data/value") == std::to_string(cpuTempValue)); */

    /* std::map<std::string, std::string> expected = { */
    /*     {"[name='ne']", ""}, */
    /*     {"[name='ne']/name", "ne"}, */
    /*     {"[name='ne']/class", "iana-hardware:chassis"}, */
    /*     {"[name='ne']/mfg-name", "CESNET"}, */

    /*     {"[name='ne:temperature-cpu']", ""}, */
    /*     {"[name='ne:temperature-cpu']/name", "ne:temperature-cpu"}, */
    /*     {"[name='ne:temperature-cpu']/class", "iana-hardware:sensor"}, */
    /*     {"[name='ne:temperature-cpu']/parent", "ne"}, */
    /*     {"[name='ne:temperature-cpu']/sensor-data", ""}, */
    /*     {"[name='ne:temperature-cpu']/sensor-data/oper-status", "ok"}, */
    /*     {"[name='ne:temperature-cpu']/sensor-data/value", "41800"}, */
    /*     {"[name='ne:temperature-cpu']/sensor-data/value-precision", "0"}, */
    /*     {"[name='ne:temperature-cpu']/sensor-data/value-scale", "milli"}, */
    /*     {"[name='ne:temperature-cpu']/sensor-data/value-type", "celsius"}, */

    /*     {"[name='ne:power']", ""}, */
    /*     {"[name='ne:power']/name", "ne:power"}, */
    /*     {"[name='ne:power']/class", "iana-hardware:sensor"}, */
    /*     {"[name='ne:power']/parent", "ne"}, */
    /*     {"[name='ne:power']/sensor-data", ""}, */
    /*     {"[name='ne:power']/sensor-data/oper-status", "ok"}, */
    /*     {"[name='ne:power']/sensor-data/value", "14000000"}, */
    /*     {"[name='ne:power']/sensor-data/value-precision", "0"}, */
    /*     {"[name='ne:power']/sensor-data/value-scale", "micro"}, */
    /*     {"[name='ne:power']/sensor-data/value-type", "watts"}, */
    /* }; */

    /* REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", sysrepo::Datastore::Operational) == expected); */

    /* REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/class") == "iana-hardware:sensor"); */
    /* REQUIRE(directLeafNodeQuery(modulePrefix + "/component[name='ne:power']/sensor-data/value") == "41800"); */
}

