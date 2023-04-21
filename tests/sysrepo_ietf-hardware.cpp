#include "trompeloeil_doctest.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

using namespace std::literals;

TEST_CASE("IETF Hardware with sysrepo")
{
    TEST_SYSREPO_INIT_LOGS;
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_CLIENT;
    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    trompeloeil::sequence seq1;
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();

    auto sysfsTempCpu = std::make_shared<FakeHWMon>();
    auto sysfsPower = std::make_shared<FakeHWMon>();

    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).RETURN(41800);
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).RETURN(14000000);

    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;

    // register components into hw state
    ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:temperature-cpu", "ne", sysfsTempCpu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:power", "ne", sysfsPower, 1));

    auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSess, ietfHardware);

    SECTION("test last-change")
    {
        // at least check that there is some timestamp
        REQUIRE(dataFromSysrepo(client, modulePrefix, sysrepo::Datastore::Operational).count("/last-change") > 0);
    }

    SECTION("test components")
    {
        std::map<std::string, std::string> expected = {
            {"[name='ne']", ""},
            {"[name='ne']/name", "ne"},
            {"[name='ne']/class", "iana-hardware:chassis"},
            {"[name='ne']/mfg-name", "CESNET"},
            {"[name='ne']/state", ""},
            {"[name='ne']/state/oper-state", "enabled"},

            {"[name='ne:temperature-cpu']", ""},
            {"[name='ne:temperature-cpu']/name", "ne:temperature-cpu"},
            {"[name='ne:temperature-cpu']/class", "iana-hardware:sensor"},
            {"[name='ne:temperature-cpu']/parent", "ne"},
            {"[name='ne:temperature-cpu']/state", ""},
            {"[name='ne:temperature-cpu']/state/oper-state", "enabled"},
            {"[name='ne:temperature-cpu']/sensor-data", ""},
            {"[name='ne:temperature-cpu']/sensor-data/oper-status", "ok"},
            {"[name='ne:temperature-cpu']/sensor-data/value", "41800"},
            {"[name='ne:temperature-cpu']/sensor-data/value-precision", "0"},
            {"[name='ne:temperature-cpu']/sensor-data/value-scale", "milli"},
            {"[name='ne:temperature-cpu']/sensor-data/value-type", "celsius"},

            {"[name='ne:power']", ""},
            {"[name='ne:power']/name", "ne:power"},
            {"[name='ne:power']/class", "iana-hardware:sensor"},
            {"[name='ne:power']/parent", "ne"},
            {"[name='ne:power']/state", ""},
            {"[name='ne:power']/state/oper-state", "enabled"},
            {"[name='ne:power']/sensor-data", ""},
            {"[name='ne:power']/sensor-data/oper-status", "ok"},
            {"[name='ne:power']/sensor-data/value", "14000000"},
            {"[name='ne:power']/sensor-data/value-precision", "0"},
            {"[name='ne:power']/sensor-data/value-scale", "micro"},
            {"[name='ne:power']/sensor-data/value-type", "watts"},
        };

        REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", sysrepo::Datastore::Operational) == expected);

        // data changes
        REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).RETURN(222);
        REQUIRE_CALL(*sysfsPower, attribute("power1_input")).RETURN(333);

        expected["[name='ne:temperature-cpu']/sensor-data/value"] = "222";
        expected["[name='ne:power']/sensor-data/value"] = "333";

        REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", sysrepo::Datastore::Operational) == expected);
    }

    SECTION("test leafnode query")
    {
        const auto xpath = modulePrefix + "/component[name='ne:power']/class";
        client.switchDatastore(sysrepo::Datastore::Operational);
        auto val = client.getData(xpath);
        client.switchDatastore(sysrepo::Datastore::Running);
        REQUIRE(val);
        REQUIRE(val->findPath(xpath)->asTerm().valueStr() == "iana-hardware:sensor"s);
    }
}

