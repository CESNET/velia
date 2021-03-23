#include "trompeloeil_doctest.h"
#include <fstream>
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

    attributesHWMon = {
        {"fan1_input"s, 253},
        {"fan2_input"s, 0},
        {"fan3_input"s, 1280},
        {"fan4_input"s, 666},
    };
    FAKE_HWMON(fans, attributesHWMon);

    attributesHWMon = {{"temp1_input", 30800}};
    FAKE_HWMON(sysfsTempFront, attributesHWMon);
    attributesHWMon = {{"temp1_input", 41800}};
    FAKE_HWMON(sysfsTempCpu, attributesHWMon);
    attributesHWMon = {{"temp1_input", 39000}};
    FAKE_HWMON(sysfsTempMII0, attributesHWMon);
    attributesHWMon = {{"temp1_input", 36000}};
    FAKE_HWMON(sysfsTempMII1, attributesHWMon);

    attributesHWMon = {{"in1_input", 220000}};
    FAKE_HWMON(sysfsVoltageAc, attributesHWMon);
    attributesHWMon = {{"in1_input", 12000}};
    FAKE_HWMON(sysfsVoltageDc, attributesHWMon);
    attributesHWMon = {{"power1_input", 14000000}};
    FAKE_HWMON(sysfsPower, attributesHWMon);
    attributesHWMon = {{"curr1_input", 200}};
    FAKE_HWMON(sysfsCurrent, attributesHWMon);

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

    MAKE_CONST_MOCK0(isPresent_mock, bool());
    MAKE_CONST_MOCK0(bind_mock, void());
    MAKE_CONST_MOCK0(unbind_mock, void());

    bool isPresent() const override
    {
        return isPresent_mock();
    }

    void bind() const override
    {
        bind_mock();
        removeDirectoryTreeIfExists(m_fakeHwmonRoot);
        std::filesystem::create_directory(m_fakeHwmonRoot);
        std::cout << "m_fakeHwmonRoot" << " = " << m_fakeHwmonRoot << "\n";
        std::filesystem::create_directory(m_fakeHwmonRoot / ("hwmon" + std::to_string(m_hwmonNo)));
        std::ofstream ofs(m_fakeHwmonRoot / ("hwmon" + std::to_string(m_hwmonNo)) / "name");
        ofs << "\n";
        m_hwmonNo++;
    }
    void unbind() const override
    {
        unbind_mock();
        removeDirectoryTreeIfExists(m_fakeHwmonRoot);
    }

private:

    std::filesystem::path m_fakeHwmonRoot;
    mutable int m_hwmonNo = 1;
};

TEST_CASE("Driver loading/unloading")
{
    const auto fakeHwmonRoot = CMAKE_CURRENT_BINARY_DIR + "/tests/psu"s;
    removeDirectoryTreeIfExists(fakeHwmonRoot);
    auto fakeI2c = std::make_shared<FakeI2C>(fakeHwmonRoot);
    trompeloeil::sequence seq1;

    // At first there is no psu present and no hwmon
    REQUIRE_CALL(*fakeI2c, isPresent_mock()).RETURN(false).IN_SEQUENCE(seq1);

    // Then, the device appears
    REQUIRE_CALL(*fakeI2c, isPresent_mock()).RETURN(true).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakeI2c, bind_mock()).IN_SEQUENCE(seq1);

    // Then, the device disappears again
    REQUIRE_CALL(*fakeI2c, isPresent_mock()).RETURN(false).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakeI2c, unbind_mock()).IN_SEQUENCE(seq1);

    // Then, it appears again
    REQUIRE_CALL(*fakeI2c, isPresent_mock()).RETURN(true).IN_SEQUENCE(seq1);
    REQUIRE_CALL(*fakeI2c, bind_mock()).IN_SEQUENCE(seq1);

    auto psu = std::make_shared<velia::ietf_hardware::FspYhPsu> (fakeHwmonRoot, "psu", fakeI2c);

    std::this_thread::sleep_for(std::chrono::seconds(10));

    psu = nullptr;

    waitForCompletionAndBitMore(seq1);
}
