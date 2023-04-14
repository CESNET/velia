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

    REQUIRE_CALL(*fans, attribute("fan1_input"s)).RETURN(253);
    REQUIRE_CALL(*fans, attribute("fan2_input"s)).RETURN(0);
    REQUIRE_CALL(*fans, attribute("fan3_input"s)).RETURN(1280);
    REQUIRE_CALL(*fans, attribute("fan4_input"s)).RETURN(666);

    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).RETURN(41800);

    REQUIRE_CALL(*sysfsVoltageAc, attribute("in1_input")).RETURN(220000);
    REQUIRE_CALL(*sysfsVoltageDc, attribute("in1_input")).RETURN(12000);
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).RETURN(14000000);
    REQUIRE_CALL(*sysfsCurrent, attribute("curr1_input")).RETURN(200);

    attributesEMMC = {{"life_time"s, "40"s}};
    FAKE_EMMC(emmc, attributesEMMC);

    using velia::ietf_hardware::data_reader::EMMC;
    using velia::ietf_hardware::data_reader::Fans;
    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::SysfsValue;
    // register components into hw state
    ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
    ietfHardware->registerDataReader(Fans("ne:fans", "ne", fans, 4));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageAC>("ne:ctrl:voltage-in", "ne:ctrl", sysfsVoltageAc, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageDC>("ne:ctrl:voltage-out", "ne:ctrl", sysfsVoltageDc, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:ctrl:power", "ne:ctrl", sysfsPower, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Current>("ne:ctrl:current", "ne:ctrl", sysfsCurrent, 1));
    ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

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

            {"[name='ne:fans']", ""},
            {"[name='ne:fans']/class", "iana-hardware:module"},
            {"[name='ne:fans']/name", "ne:fans"},
            {"[name='ne:fans']/parent", "ne"},
            {"[name='ne:fans:fan1']", ""},
            {"[name='ne:fans:fan1']/class", "iana-hardware:fan"},
            {"[name='ne:fans:fan1']/name", "ne:fans:fan1"},
            {"[name='ne:fans:fan1']/parent", "ne:fans"},
            {"[name='ne:fans:fan1:rpm']", ""},
            {"[name='ne:fans:fan1:rpm']/class", "iana-hardware:sensor"},
            {"[name='ne:fans:fan1:rpm']/name", "ne:fans:fan1:rpm"},
            {"[name='ne:fans:fan1:rpm']/parent", "ne:fans:fan1"},
            {"[name='ne:fans:fan1:rpm']/sensor-data", ""},
            {"[name='ne:fans:fan1:rpm']/sensor-data/oper-status", "ok"},
            {"[name='ne:fans:fan1:rpm']/sensor-data/value", "253"},
            {"[name='ne:fans:fan1:rpm']/sensor-data/value-precision", "0"},
            {"[name='ne:fans:fan1:rpm']/sensor-data/value-scale", "units"},
            {"[name='ne:fans:fan1:rpm']/sensor-data/value-type", "rpm"},
            {"[name='ne:fans:fan2']", ""},
            {"[name='ne:fans:fan2']/class", "iana-hardware:fan"},
            {"[name='ne:fans:fan2']/name", "ne:fans:fan2"},
            {"[name='ne:fans:fan2']/parent", "ne:fans"},
            {"[name='ne:fans:fan2:rpm']", ""},
            {"[name='ne:fans:fan2:rpm']/class", "iana-hardware:sensor"},
            {"[name='ne:fans:fan2:rpm']/name", "ne:fans:fan2:rpm"},
            {"[name='ne:fans:fan2:rpm']/parent", "ne:fans:fan2"},
            {"[name='ne:fans:fan2:rpm']/sensor-data", ""},
            {"[name='ne:fans:fan2:rpm']/sensor-data/oper-status", "ok"},
            {"[name='ne:fans:fan2:rpm']/sensor-data/value", "0"},
            {"[name='ne:fans:fan2:rpm']/sensor-data/value-precision", "0"},
            {"[name='ne:fans:fan2:rpm']/sensor-data/value-scale", "units"},
            {"[name='ne:fans:fan2:rpm']/sensor-data/value-type", "rpm"},
            {"[name='ne:fans:fan3']", ""},
            {"[name='ne:fans:fan3']/class", "iana-hardware:fan"},
            {"[name='ne:fans:fan3']/name", "ne:fans:fan3"},
            {"[name='ne:fans:fan3']/parent", "ne:fans"},
            {"[name='ne:fans:fan3:rpm']", ""},
            {"[name='ne:fans:fan3:rpm']/class", "iana-hardware:sensor"},
            {"[name='ne:fans:fan3:rpm']/name", "ne:fans:fan3:rpm"},
            {"[name='ne:fans:fan3:rpm']/parent", "ne:fans:fan3"},
            {"[name='ne:fans:fan3:rpm']/sensor-data", ""},
            {"[name='ne:fans:fan3:rpm']/sensor-data/oper-status", "ok"},
            {"[name='ne:fans:fan3:rpm']/sensor-data/value", "1280"},
            {"[name='ne:fans:fan3:rpm']/sensor-data/value-precision", "0"},
            {"[name='ne:fans:fan3:rpm']/sensor-data/value-scale", "units"},
            {"[name='ne:fans:fan3:rpm']/sensor-data/value-type", "rpm"},
            {"[name='ne:fans:fan4']", ""},
            {"[name='ne:fans:fan4']/class", "iana-hardware:fan"},
            {"[name='ne:fans:fan4']/name", "ne:fans:fan4"},
            {"[name='ne:fans:fan4']/parent", "ne:fans"},
            {"[name='ne:fans:fan4:rpm']", ""},
            {"[name='ne:fans:fan4:rpm']/class", "iana-hardware:sensor"},
            {"[name='ne:fans:fan4:rpm']/name", "ne:fans:fan4:rpm"},
            {"[name='ne:fans:fan4:rpm']/parent", "ne:fans:fan4"},
            {"[name='ne:fans:fan4:rpm']/sensor-data", ""},
            {"[name='ne:fans:fan4:rpm']/sensor-data/oper-status", "ok"},
            {"[name='ne:fans:fan4:rpm']/sensor-data/value", "666"},
            {"[name='ne:fans:fan4:rpm']/sensor-data/value-precision", "0"},
            {"[name='ne:fans:fan4:rpm']/sensor-data/value-scale", "units"},
            {"[name='ne:fans:fan4:rpm']/sensor-data/value-type", "rpm"},

            {"[name='ne:ctrl']", ""},
            {"[name='ne:ctrl']/name", "ne:ctrl"},
            {"[name='ne:ctrl']/parent", "ne"},
            {"[name='ne:ctrl']/class", "iana-hardware:module"},

            {"[name='ne:ctrl:temperature-cpu']", ""},
            {"[name='ne:ctrl:temperature-cpu']/name", "ne:ctrl:temperature-cpu"},
            {"[name='ne:ctrl:temperature-cpu']/class", "iana-hardware:sensor"},
            {"[name='ne:ctrl:temperature-cpu']/parent", "ne:ctrl"},
            {"[name='ne:ctrl:temperature-cpu']/sensor-data", ""},
            {"[name='ne:ctrl:temperature-cpu']/sensor-data/oper-status", "ok"},
            {"[name='ne:ctrl:temperature-cpu']/sensor-data/value", "41800"},
            {"[name='ne:ctrl:temperature-cpu']/sensor-data/value-precision", "0"},
            {"[name='ne:ctrl:temperature-cpu']/sensor-data/value-scale", "milli"},
            {"[name='ne:ctrl:temperature-cpu']/sensor-data/value-type", "celsius"},

            {"[name='ne:ctrl:power']", ""},
            {"[name='ne:ctrl:power']/name", "ne:ctrl:power"},
            {"[name='ne:ctrl:power']/class", "iana-hardware:sensor"},
            {"[name='ne:ctrl:power']/parent", "ne:ctrl"},
            {"[name='ne:ctrl:power']/sensor-data", ""},
            {"[name='ne:ctrl:power']/sensor-data/oper-status", "ok"},
            {"[name='ne:ctrl:power']/sensor-data/value", "14000000"},
            {"[name='ne:ctrl:power']/sensor-data/value-precision", "0"},
            {"[name='ne:ctrl:power']/sensor-data/value-scale", "micro"},
            {"[name='ne:ctrl:power']/sensor-data/value-type", "watts"},

            {"[name='ne:ctrl:voltage-in']", ""},
            {"[name='ne:ctrl:voltage-in']/name", "ne:ctrl:voltage-in"},
            {"[name='ne:ctrl:voltage-in']/class", "iana-hardware:sensor"},
            {"[name='ne:ctrl:voltage-in']/parent", "ne:ctrl"},
            {"[name='ne:ctrl:voltage-in']/sensor-data", ""},
            {"[name='ne:ctrl:voltage-in']/sensor-data/oper-status", "ok"},
            {"[name='ne:ctrl:voltage-in']/sensor-data/value", "220000"},
            {"[name='ne:ctrl:voltage-in']/sensor-data/value-precision", "0"},
            {"[name='ne:ctrl:voltage-in']/sensor-data/value-scale", "milli"},
            {"[name='ne:ctrl:voltage-in']/sensor-data/value-type", "volts-AC"},
            {"[name='ne:ctrl:voltage-out']", ""},
            {"[name='ne:ctrl:voltage-out']/name", "ne:ctrl:voltage-out"},
            {"[name='ne:ctrl:voltage-out']/class", "iana-hardware:sensor"},
            {"[name='ne:ctrl:voltage-out']/parent", "ne:ctrl"},
            {"[name='ne:ctrl:voltage-out']/sensor-data", ""},
            {"[name='ne:ctrl:voltage-out']/sensor-data/oper-status", "ok"},
            {"[name='ne:ctrl:voltage-out']/sensor-data/value", "12000"},
            {"[name='ne:ctrl:voltage-out']/sensor-data/value-precision", "0"},
            {"[name='ne:ctrl:voltage-out']/sensor-data/value-scale", "milli"},
            {"[name='ne:ctrl:voltage-out']/sensor-data/value-type", "volts-DC"},

            {"[name='ne:ctrl:current']", ""},
            {"[name='ne:ctrl:current']/name", "ne:ctrl:current"},
            {"[name='ne:ctrl:current']/class", "iana-hardware:sensor"},
            {"[name='ne:ctrl:current']/parent", "ne:ctrl"},
            {"[name='ne:ctrl:current']/sensor-data", ""},
            {"[name='ne:ctrl:current']/sensor-data/oper-status", "ok"},
            {"[name='ne:ctrl:current']/sensor-data/value", "200"},
            {"[name='ne:ctrl:current']/sensor-data/value-precision", "0"},
            {"[name='ne:ctrl:current']/sensor-data/value-scale", "milli"},
            {"[name='ne:ctrl:current']/sensor-data/value-type", "amperes"},

            {"[name='ne:ctrl:emmc']", ""},
            {"[name='ne:ctrl:emmc']/name", "ne:ctrl:emmc"},
            {"[name='ne:ctrl:emmc']/parent", "ne:ctrl"},
            {"[name='ne:ctrl:emmc']/class", "iana-hardware:module"},
            {"[name='ne:ctrl:emmc']/serial-num", "0x00a8808d"},
            {"[name='ne:ctrl:emmc']/mfg-date", "2017-02-01T00:00:00-00:00"},
            {"[name='ne:ctrl:emmc']/model-name", "8GME4R"},
            {"[name='ne:ctrl:emmc:lifetime']", ""},
            {"[name='ne:ctrl:emmc:lifetime']/name", "ne:ctrl:emmc:lifetime"},
            {"[name='ne:ctrl:emmc:lifetime']/class", "iana-hardware:sensor"},
            {"[name='ne:ctrl:emmc:lifetime']/parent", "ne:ctrl:emmc"},
            {"[name='ne:ctrl:emmc:lifetime']/sensor-data", ""},
            {"[name='ne:ctrl:emmc:lifetime']/sensor-data/oper-status", "ok"},
            {"[name='ne:ctrl:emmc:lifetime']/sensor-data/value", "40"},
            {"[name='ne:ctrl:emmc:lifetime']/sensor-data/value-precision", "0"},
            {"[name='ne:ctrl:emmc:lifetime']/sensor-data/value-scale", "units"},
            {"[name='ne:ctrl:emmc:lifetime']/sensor-data/value-type", "other"},
            {"[name='ne:ctrl:emmc:lifetime']/sensor-data/units-display", "percent"},
        };

        REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", sysrepo::Datastore::Operational) == expected);
    }

    SECTION("test leafnode query")
    {
        const auto xpath = modulePrefix + "/component[name='ne:ctrl:emmc:lifetime']/class";
        client.switchDatastore(sysrepo::Datastore::Operational);
        auto val = client.getData(xpath);
        client.switchDatastore(sysrepo::Datastore::Running);
        REQUIRE(val);
        REQUIRE(val->findPath(xpath)->asTerm().valueStr() == "iana-hardware:sensor"s);
    }
}

