#include <iostream>
#include "trompeloeil_doctest.h"
#include <fstream>
#include <future>
#include <iterator>
#include "fs-helpers/utils.h"
#include "ietf-hardware/FspYhPsu.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("HardwareState")
{
    TEST_INIT_LOGS;
    static const auto modulePrefix = "/ietf-hardware:hardware"s;

    trompeloeil::sequence seq1;
    auto ietfHardware = std::make_shared<velia::ietf_hardware::IETFHardware>();

    auto fans = std::make_shared<FakeHWMon>();
    auto sysfsTempCpu = std::make_shared<FakeHWMon>();
    auto sysfsTempFront = std::make_shared<FakeHWMon>();
    auto sysfsTempMII0 = std::make_shared<FakeHWMon>();
    auto sysfsTempMII1 = std::make_shared<FakeHWMon>();
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

    REQUIRE_CALL(*fans, attribute("fan1_input"s)).RETURN( 253);
    REQUIRE_CALL(*fans, attribute("fan2_input"s)).RETURN( 0);
    REQUIRE_CALL(*fans, attribute("fan3_input"s)).RETURN( 1280);
    REQUIRE_CALL(*fans, attribute("fan4_input"s)).RETURN( 666);

    REQUIRE_CALL(*sysfsTempFront, attribute("temp1_input")).RETURN(30800);
    REQUIRE_CALL(*sysfsTempCpu, attribute("temp1_input")).RETURN(41800);
    REQUIRE_CALL(*sysfsTempMII0, attribute("temp1_input")).RETURN(39000);
    REQUIRE_CALL(*sysfsTempMII1, attribute("temp1_input")).RETURN(36000);

    REQUIRE_CALL(*sysfsVoltageAc, attribute("in1_input")).RETURN(220000);
    REQUIRE_CALL(*sysfsVoltageDc, attribute("in1_input")).RETURN(12000);
    REQUIRE_CALL(*sysfsPower, attribute("power1_input")).RETURN(14000000);
    REQUIRE_CALL(*sysfsCurrent, attribute("curr1_input")).RETURN(200);

    attributesEMMC = {{"life_time"s, "40"s}};
    FAKE_EMMC(emmc, attributesEMMC);

    using velia::ietf_hardware::data_reader::SensorType;
    using velia::ietf_hardware::data_reader::StaticData;
    using velia::ietf_hardware::data_reader::Fans;
    using velia::ietf_hardware::data_reader::SysfsValue;
    using velia::ietf_hardware::data_reader::EMMC;
    // register components into hw state
    ietfHardware->registerDataReader(StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    ietfHardware->registerDataReader(StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
    ietfHardware->registerDataReader(Fans("ne:fans", "ne", fans, 4));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Temperature>("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageAC>("ne:ctrl:voltage-in", "ne:ctrl", sysfsVoltageAc, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::VoltageDC>("ne:ctrl:voltage-out", "ne:ctrl", sysfsVoltageDc, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Power>("ne:ctrl:power", "ne:ctrl", sysfsPower, 1));
    ietfHardware->registerDataReader(SysfsValue<SensorType::Current>("ne:ctrl:current", "ne:ctrl", sysfsCurrent, 1));
    ietfHardware->registerDataReader(EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

    SECTION("Test HardwareState without sysrepo")
    {
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
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-front']/class", "iana-hardware:sensor"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-front']/parent", "ne:ctrl"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value", "30800"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value-precision", "0"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value-scale", "milli"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value-type", "celsius"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-0']/class", "iana-hardware:sensor"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-0']/parent", "ne:ctrl"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value", "39000"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value-precision", "0"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value-scale", "milli"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value-type", "celsius"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-1']/class", "iana-hardware:sensor"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-1']/parent", "ne:ctrl"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value", "36000"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value-precision", "0"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value-scale", "milli"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value-type", "celsius"},

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
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-scale", "micro"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-in']/sensor-data/value-type", "volts-AC"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/class", "iana-hardware:sensor"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/parent", "ne:ctrl"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value", "12000"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-precision", "0"},
            {"/ietf-hardware:hardware/component[name='ne:ctrl:voltage-out']/sensor-data/value-scale", "micro"},
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
            {"/ietf-hardware:hardware/component[name='ne:ctrl:emmc']/mfg-date", "2017-02-01T00:00:00Z"},
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
        result.erase(modulePrefix + "/last-change");
        REQUIRE(result == expected);
    }

    SECTION("Test IETF Hardware from sysrepo's view")
    {
        TEST_SYSREPO_INIT_LOGS;
        TEST_SYSREPO_INIT;
        TEST_SYSREPO_INIT_CLIENT;

        auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSubs, ietfHardware);

        SECTION("test last-change")
        {
            // at least check that there is some timestamp
            REQUIRE(dataFromSysrepo(client, modulePrefix, SR_DS_OPERATIONAL).count("/last-change") > 0);
        }

        SECTION("test components")
        {
            std::map<std::string, std::string> expected = {
                {"[name='ne']/name", "ne"},
                {"[name='ne']/class", "iana-hardware:chassis"},
                {"[name='ne']/mfg-name", "CESNET"},
                {"[name='ne']/sensor-data", ""},

                {"[name='ne:fans']/class", "iana-hardware:module"},
                {"[name='ne:fans']/name", "ne:fans"},
                {"[name='ne:fans']/parent", "ne"},
                {"[name='ne:fans']/sensor-data", ""},
                {"[name='ne:fans:fan1']/class", "iana-hardware:fan"},
                {"[name='ne:fans:fan1']/name", "ne:fans:fan1"},
                {"[name='ne:fans:fan1']/parent", "ne:fans"},
                {"[name='ne:fans:fan1']/sensor-data", ""},
                {"[name='ne:fans:fan1:rpm']/class", "iana-hardware:sensor"},
                {"[name='ne:fans:fan1:rpm']/name", "ne:fans:fan1:rpm"},
                {"[name='ne:fans:fan1:rpm']/parent", "ne:fans:fan1"},
                {"[name='ne:fans:fan1:rpm']/sensor-data", ""},
                {"[name='ne:fans:fan1:rpm']/sensor-data/oper-status", "ok"},
                {"[name='ne:fans:fan1:rpm']/sensor-data/value", "253"},
                {"[name='ne:fans:fan1:rpm']/sensor-data/value-precision", "0"},
                {"[name='ne:fans:fan1:rpm']/sensor-data/value-scale", "units"},
                {"[name='ne:fans:fan1:rpm']/sensor-data/value-type", "rpm"},
                {"[name='ne:fans:fan2']/class", "iana-hardware:fan"},
                {"[name='ne:fans:fan2']/name", "ne:fans:fan2"},
                {"[name='ne:fans:fan2']/parent", "ne:fans"},
                {"[name='ne:fans:fan2']/sensor-data", ""},
                {"[name='ne:fans:fan2:rpm']/class", "iana-hardware:sensor"},
                {"[name='ne:fans:fan2:rpm']/name", "ne:fans:fan2:rpm"},
                {"[name='ne:fans:fan2:rpm']/parent", "ne:fans:fan2"},
                {"[name='ne:fans:fan2:rpm']/sensor-data", ""},
                {"[name='ne:fans:fan2:rpm']/sensor-data/oper-status", "ok"},
                {"[name='ne:fans:fan2:rpm']/sensor-data/value", "0"},
                {"[name='ne:fans:fan2:rpm']/sensor-data/value-precision", "0"},
                {"[name='ne:fans:fan2:rpm']/sensor-data/value-scale", "units"},
                {"[name='ne:fans:fan2:rpm']/sensor-data/value-type", "rpm"},
                {"[name='ne:fans:fan3']/class", "iana-hardware:fan"},
                {"[name='ne:fans:fan3']/name", "ne:fans:fan3"},
                {"[name='ne:fans:fan3']/parent", "ne:fans"},
                {"[name='ne:fans:fan3']/sensor-data", ""},
                {"[name='ne:fans:fan3:rpm']/class", "iana-hardware:sensor"},
                {"[name='ne:fans:fan3:rpm']/name", "ne:fans:fan3:rpm"},
                {"[name='ne:fans:fan3:rpm']/parent", "ne:fans:fan3"},
                {"[name='ne:fans:fan3:rpm']/sensor-data", ""},
                {"[name='ne:fans:fan3:rpm']/sensor-data/oper-status", "ok"},
                {"[name='ne:fans:fan3:rpm']/sensor-data/value", "1280"},
                {"[name='ne:fans:fan3:rpm']/sensor-data/value-precision", "0"},
                {"[name='ne:fans:fan3:rpm']/sensor-data/value-scale", "units"},
                {"[name='ne:fans:fan3:rpm']/sensor-data/value-type", "rpm"},
                {"[name='ne:fans:fan4']/class", "iana-hardware:fan"},
                {"[name='ne:fans:fan4']/name", "ne:fans:fan4"},
                {"[name='ne:fans:fan4']/parent", "ne:fans"},
                {"[name='ne:fans:fan4']/sensor-data", ""},
                {"[name='ne:fans:fan4:rpm']/class", "iana-hardware:sensor"},
                {"[name='ne:fans:fan4:rpm']/name", "ne:fans:fan4:rpm"},
                {"[name='ne:fans:fan4:rpm']/parent", "ne:fans:fan4"},
                {"[name='ne:fans:fan4:rpm']/sensor-data", ""},
                {"[name='ne:fans:fan4:rpm']/sensor-data/oper-status", "ok"},
                {"[name='ne:fans:fan4:rpm']/sensor-data/value", "666"},
                {"[name='ne:fans:fan4:rpm']/sensor-data/value-precision", "0"},
                {"[name='ne:fans:fan4:rpm']/sensor-data/value-scale", "units"},
                {"[name='ne:fans:fan4:rpm']/sensor-data/value-type", "rpm"},

                {"[name='ne:ctrl']/name", "ne:ctrl"},
                {"[name='ne:ctrl']/parent", "ne"},
                {"[name='ne:ctrl']/class", "iana-hardware:module"},
                {"[name='ne:ctrl']/sensor-data", ""},

                {"[name='ne:ctrl:temperature-cpu']/name", "ne:ctrl:temperature-cpu"},
                {"[name='ne:ctrl:temperature-cpu']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:temperature-cpu']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:temperature-cpu']/sensor-data", ""},
                {"[name='ne:ctrl:temperature-cpu']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:temperature-cpu']/sensor-data/value", "41800"},
                {"[name='ne:ctrl:temperature-cpu']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:temperature-cpu']/sensor-data/value-scale", "milli"},
                {"[name='ne:ctrl:temperature-cpu']/sensor-data/value-type", "celsius"},
                {"[name='ne:ctrl:temperature-front']/name", "ne:ctrl:temperature-front"},
                {"[name='ne:ctrl:temperature-front']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:temperature-front']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:temperature-front']/sensor-data", ""},
                {"[name='ne:ctrl:temperature-front']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:temperature-front']/sensor-data/value", "30800"},
                {"[name='ne:ctrl:temperature-front']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:temperature-front']/sensor-data/value-scale", "milli"},
                {"[name='ne:ctrl:temperature-front']/sensor-data/value-type", "celsius"},
                {"[name='ne:ctrl:temperature-internal-0']/name", "ne:ctrl:temperature-internal-0"},
                {"[name='ne:ctrl:temperature-internal-0']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:temperature-internal-0']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:temperature-internal-0']/sensor-data", ""},
                {"[name='ne:ctrl:temperature-internal-0']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:temperature-internal-0']/sensor-data/value", "39000"},
                {"[name='ne:ctrl:temperature-internal-0']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:temperature-internal-0']/sensor-data/value-scale", "milli"},
                {"[name='ne:ctrl:temperature-internal-0']/sensor-data/value-type", "celsius"},
                {"[name='ne:ctrl:temperature-internal-1']/name", "ne:ctrl:temperature-internal-1"},
                {"[name='ne:ctrl:temperature-internal-1']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:temperature-internal-1']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:temperature-internal-1']/sensor-data", ""},
                {"[name='ne:ctrl:temperature-internal-1']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:temperature-internal-1']/sensor-data/value", "36000"},
                {"[name='ne:ctrl:temperature-internal-1']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:temperature-internal-1']/sensor-data/value-scale", "milli"},
                {"[name='ne:ctrl:temperature-internal-1']/sensor-data/value-type", "celsius"},

                {"[name='ne:ctrl:power']/name", "ne:ctrl:power"},
                {"[name='ne:ctrl:power']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:power']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:power']/sensor-data", ""},
                {"[name='ne:ctrl:power']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:power']/sensor-data/value", "14000000"},
                {"[name='ne:ctrl:power']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:power']/sensor-data/value-scale", "micro"},
                {"[name='ne:ctrl:power']/sensor-data/value-type", "watts"},

                {"[name='ne:ctrl:voltage-in']/name", "ne:ctrl:voltage-in"},
                {"[name='ne:ctrl:voltage-in']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:voltage-in']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:voltage-in']/sensor-data", ""},
                {"[name='ne:ctrl:voltage-in']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:voltage-in']/sensor-data/value", "220000"},
                {"[name='ne:ctrl:voltage-in']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:voltage-in']/sensor-data/value-scale", "micro"},
                {"[name='ne:ctrl:voltage-in']/sensor-data/value-type", "volts-AC"},
                {"[name='ne:ctrl:voltage-out']/name", "ne:ctrl:voltage-out"},
                {"[name='ne:ctrl:voltage-out']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:voltage-out']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:voltage-out']/sensor-data", ""},
                {"[name='ne:ctrl:voltage-out']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:voltage-out']/sensor-data/value", "12000"},
                {"[name='ne:ctrl:voltage-out']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:voltage-out']/sensor-data/value-scale", "micro"},
                {"[name='ne:ctrl:voltage-out']/sensor-data/value-type", "volts-DC"},

                {"[name='ne:ctrl:current']/name", "ne:ctrl:current"},
                {"[name='ne:ctrl:current']/class", "iana-hardware:sensor"},
                {"[name='ne:ctrl:current']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:current']/sensor-data", ""},
                {"[name='ne:ctrl:current']/sensor-data/oper-status", "ok"},
                {"[name='ne:ctrl:current']/sensor-data/value", "200"},
                {"[name='ne:ctrl:current']/sensor-data/value-precision", "0"},
                {"[name='ne:ctrl:current']/sensor-data/value-scale", "milli"},
                {"[name='ne:ctrl:current']/sensor-data/value-type", "amperes"},

                {"[name='ne:ctrl:emmc']/name", "ne:ctrl:emmc"},
                {"[name='ne:ctrl:emmc']/parent", "ne:ctrl"},
                {"[name='ne:ctrl:emmc']/class", "iana-hardware:module"},
                {"[name='ne:ctrl:emmc']/serial-num", "0x00a8808d"},
                {"[name='ne:ctrl:emmc']/mfg-date", "2017-02-01T00:00:00Z"},
                {"[name='ne:ctrl:emmc']/model-name", "8GME4R"},
                {"[name='ne:ctrl:emmc']/sensor-data", ""},
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

            REQUIRE(dataFromSysrepo(client, modulePrefix + "/component", SR_DS_OPERATIONAL) == expected);
        }

        SECTION("test leafnode query")
        {
            const auto xpath = modulePrefix + "/component[name='ne:ctrl:emmc:lifetime']/class";
            client->session_switch_ds(SR_DS_OPERATIONAL);
            auto val = client->get_item(xpath.c_str());
            client->session_switch_ds(SR_DS_RUNNING);
            REQUIRE(!!val);
            REQUIRE(val->data()->get_identityref() == "iana-hardware:sensor"s);
        }
    }
}

class FakeI2C : public velia::ietf_hardware::TransientI2C {
public:
    FakeI2C(const std::string& fakeHwmonRoot)
        : TransientI2C({}, {}, {})
        , m_fakeHwmonRoot(fakeHwmonRoot)
    {
    }

    MAKE_CONST_MOCK0(isPresent, bool(), override);
    MAKE_CONST_MOCK0(bind_mock, void());
    MAKE_CONST_MOCK0(unbind_mock, void());

    void removeHwmonFile(const std::string& name) const
    {
        std::filesystem::remove(m_fakeHwmonRoot / ("hwmon" + std::to_string(m_hwmonNo)) / name);
    }

    void bind() const override
    {
        bind_mock();
        removeDirectoryTreeIfExists(m_fakeHwmonRoot);
        std::filesystem::create_directory(m_fakeHwmonRoot);
        std::filesystem::create_directory(m_fakeHwmonRoot / ("hwmon" + std::to_string(m_hwmonNo)));

        for (const auto& filename : {"name", "temp1_input", "temp2_input", "curr1_input", "curr2_input", "curr3_input",
                "in1_input", "in2_input", "in3_input", "power1_input", "power2_input", "fan1_input"} )
        {
            std::ofstream ofs(m_fakeHwmonRoot / ("hwmon" + std::to_string(m_hwmonNo)) / filename);
            // I don't really care about the values here, I just need the HWMon class to think that the files exist.
            ofs << 0 << "\n";
        }
    }
    void unbind() const override
    {
        unbind_mock();
        removeDirectoryTreeIfExists(m_fakeHwmonRoot);
        m_hwmonNo++;
    }

private:

    std::filesystem::path m_fakeHwmonRoot;
    mutable std::atomic<int> m_hwmonNo = 1;
};

TEST_CASE("FspYhPsu")
{
    TEST_INIT_LOGS;
    std::atomic<int> counter = 0;
    const auto fakeHwmonRoot = CMAKE_CURRENT_BINARY_DIR + "/tests/psu"s;
    removeDirectoryTreeIfExists(fakeHwmonRoot);
    auto fakeI2c = std::make_shared<FakeI2C>(fakeHwmonRoot);
    trompeloeil::sequence seq1;
    std::shared_ptr<velia::ietf_hardware::FspYhPsu> psu;
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

    auto i2cPresence = [&counter] {
        switch (counter) {
        case 0:
        case 2:
        case 4:
            return false;
        case 1:
        case 3:
            return true;
        }

        REQUIRE(false);
        __builtin_unreachable();
    };

    ALLOW_CALL(*fakeI2c, isPresent()).LR_RETURN(i2cPresence());
    REQUIRE_CALL(*fakeI2c, bind_mock()).LR_WITH(counter == 1).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakeI2c, unbind_mock()).LR_WITH(counter == 2).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakeI2c, bind_mock()).LR_WITH(counter == 3).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakeI2c, unbind_mock()).LR_WITH(counter == 4).IN_SEQUENCE(seq1);

    psu = std::make_shared<velia::ietf_hardware::FspYhPsu>(fakeHwmonRoot, "psu", fakeI2c);

    for (auto i : {0, 1, 2, 3, 4}) {
        std::this_thread::sleep_for(std::chrono::seconds(4));
        velia::ietf_hardware::DataTree expected;

        switch (i) {
        case 0:
            break;
        case 1:
            expected = {
                {"/ietf-hardware:hardware/component[name='ne:psu']/class", "iana-hardware:power-supply"},
                {"/ietf-hardware:hardware/component[name='ne:psu']/parent", "ne"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-12V']/sensor-data/value-type", "amperes"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-5Vsb']/sensor-data/value-type", "amperes"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:current-in']/sensor-data/value-type", "amperes"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1']/class", "iana-hardware:fan"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1']/parent", "ne:psu:fan"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/parent", "ne:psu:fan:fan1"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value-scale", "units"},
                {"/ietf-hardware:hardware/component[name='ne:psu:fan:fan1:rpm']/sensor-data/value-type", "rpm"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value-scale", "micro"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-in']/sensor-data/value-type", "watts"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value-scale", "micro"},
                {"/ietf-hardware:hardware/component[name='ne:psu:power-out']/sensor-data/value-type", "watts"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-1']/sensor-data/value-type", "celsius"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value-scale", "milli"},
                {"/ietf-hardware:hardware/component[name='ne:psu:temperature-2']/sensor-data/value-type", "celsius"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value-scale", "micro"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-12V']/sensor-data/value-type", "volts-DC"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value-scale", "micro"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-5Vsb']/sensor-data/value-type", "volts-DC"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/class", "iana-hardware:sensor"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/parent", "ne:psu"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/oper-status", "ok"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value-precision", "0"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value-scale", "micro"},
                {"/ietf-hardware:hardware/component[name='ne:psu:voltage-in']/sensor-data/value-type", "volts-AC"},
            };
            break;
        case 2:
            break;
        case 3:
            // Here I simulate read failure by a file from the hwmon directory. This happens when the user wants data from
            // a PSU that's no longer there and the watcher thread didn't unbind it yet.
            fakeI2c->removeHwmonFile("temp1_input");
            break;
        case 4:
            break;
        }

        REQUIRE(psu->readValues() == expected);

        counter++;
    }

    waitForCompletionAndBitMore(seq1);
}
