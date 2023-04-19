#include "trompeloeil_doctest.h"
#include "ietf-hardware/IETFHardware.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"

using namespace std::literals;

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

    std::map<std::string, std::string> expected = {
        {"/ietf-hardware:hardware/component[name='ne']/class", "iana-hardware:chassis"},
        {"/ietf-hardware:hardware/component[name='ne']/mfg-name", "CESNET"},

        {"/ietf-hardware:hardware/component[name='ne:fans']/class", "iana-hardware:module"},
        {"/ietf-hardware:hardware/component[name='ne:fans']/parent", "ne"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/parent", "ne:fans:fan1"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value", "253"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-type", "rpm"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/parent", "ne:fans:fan2"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-type", "rpm"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/parent", "ne:fans:fan3"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value", "1280"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-type", "rpm"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4']/class", "iana-hardware:fan"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4']/parent", "ne:fans"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/parent", "ne:fans:fan4"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value", "666"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-type", "rpm"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl']/parent", "ne"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl']/class", "iana-hardware:module"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value", "41800"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-type", "celsius"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value", "14000000"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value-scale", "micro"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:power']/sensor-data/value-type", "watts"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value", "220000"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-type", "volts-AC"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value", "12000"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-type", "volts-DC"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value", "200"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value-scale", "milli"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:current']/sensor-data/value-type", "amperes"},

        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/parent", "ne:ctrl"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/class", "iana-hardware:module"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/serial-num", "0x00a8808d"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/mfg-date", "2017-02-01T00:00:00-00:00"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/model-name", "8GME4R"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/class", "iana-hardware:sensor"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/parent", "ne:ctrl:emmc"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/oper-status", "ok"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value", "40"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-precision", "0"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-scale", "units"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-type", "other"},
        {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/units-display", "percent"},
    };

    // exclude last-change node
    auto result = ietfHardware->process();
    result.dataTree.erase(modulePrefix + "/last-change");
    REQUIRE(result.dataTree == expected);
}
