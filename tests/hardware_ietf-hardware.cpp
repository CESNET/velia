#include "trompeloeil_doctest.h"
#include <iterator>
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
    static const auto modulePrefix = "/ietf-hardware-state:hardware"s;

    trompeloeil::sequence seq1;
    auto hwState = std::make_shared<velia::ietf_hardware::IETFHardware>();

    auto fans = std::make_shared<FakeHWMon>();
    auto sysfsTempCpu = std::make_shared<FakeHWMon>();
    auto sysfsTempFront = std::make_shared<FakeHWMon>();
    auto sysfsTempMII0 = std::make_shared<FakeHWMon>();
    auto sysfsTempMII1 = std::make_shared<FakeHWMon>();
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

    attributesEMMC = {{"life_time"s, "40"s}};
    FAKE_EMMC(emmc, attributesEMMC);

    // register components into hw state
    hwState->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne", std::nullopt, {{"class", "iana-hardware:chassis"}, {"mfg-name", "CESNET"s}}));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::StaticData("ne:ctrl", "ne", {{"class", "iana-hardware:module"}}));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::Fans("ne:fans", "ne", fans, 4));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::SysfsTemperature("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
    hwState->registerDataReader(velia::ietf_hardware::data_reader::EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

    SECTION("Test HardwareState without sysrepo")
    {
        std::map<std::string, std::string> expected = {
            {"/ietf-hardware-state:hardware/component[name='ne']/class", "iana-hardware:chassis"},
            {"/ietf-hardware-state:hardware/component[name='ne']/mfg-name", "CESNET"},

            {"/ietf-hardware-state:hardware/component[name='ne:fans']/class", "iana-hardware:module"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans']/parent", "ne"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1']/class", "iana-hardware:fan"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/parent", "ne:fans:fan1"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value", "253"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-type", "rpm"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2']/class", "iana-hardware:fan"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/parent", "ne:fans:fan2"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-type", "rpm"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3']/class", "iana-hardware:fan"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/parent", "ne:fans:fan3"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value", "1280"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-type", "rpm"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4']/class", "iana-hardware:fan"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/parent", "ne:fans:fan4"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value", "666"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/sensor-data/value-type", "rpm"},

            {"/ietf-hardware-state:hardware/component[name='ne:ctrl']/parent", "ne"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl']/class", "iana-hardware:module"},

            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-cpu']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-cpu']/parent", "ne:ctrl"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value", "41800"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-scale", "milli"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-cpu']/sensor-data/value-type", "celsius"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-front']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-front']/parent", "ne:ctrl"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value", "30800"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value-scale", "milli"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-front']/sensor-data/value-type", "celsius"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-0']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-0']/parent", "ne:ctrl"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value", "39000"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value-scale", "milli"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-0']/sensor-data/value-type", "celsius"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-1']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-1']/parent", "ne:ctrl"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value", "36000"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value-scale", "milli"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:temperature-internal-1']/sensor-data/value-type", "celsius"},

            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc']/parent", "ne:ctrl"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc']/class", "iana-hardware:module"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc']/serial-num", "0x00a8808d"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc']/mfg-date", "2017-02-01T00:00:00Z"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc']/model-name", "8GME4R"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/parent", "ne:ctrl:emmc"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value", "40"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/value-type", "other"},
            {"/ietf-hardware-state:hardware/component[name='ne:ctrl:emmc:lifetime']/sensor-data/units-display", "percent"},
        };

        // exclude last-change node
        auto result = hwState->process();
        result.erase(modulePrefix + "/last-change");
        REQUIRE(result == expected);
    }

    SECTION("Test HardwareState from sysrepo's view")
    {
        TEST_SYSREPO_INIT_LOGS;

        auto srConn = std::make_shared<sysrepo::Connection>();
        auto srSess = std::make_shared<sysrepo::Session>(srConn);
        auto srSubs = std::make_shared<sysrepo::Subscribe>(srSess);
        auto ietfHardwareSysrepo = std::make_shared<velia::ietf_hardware::sysrepo::Sysrepo>(srSubs, hwState);

        SECTION("test last-change")
        {
            // at least check that there is some timestamp
            srSess->session_switch_ds(SR_DS_OPERATIONAL);
            REQUIRE(dataFromSysrepo(srSess, modulePrefix).count("/last-change") > 0);
            srSess->session_switch_ds(SR_DS_RUNNING);
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

            srSess->session_switch_ds(SR_DS_OPERATIONAL);
            REQUIRE(dataFromSysrepo(srSess, modulePrefix + "/component") == expected);
            srSess->session_switch_ds(SR_DS_RUNNING);
        }

        SECTION("test leafnode query")
        {
            const auto xpath = modulePrefix + "/component[name='ne:ctrl:emmc:lifetime']/class";
            srSess->session_switch_ds(SR_DS_OPERATIONAL);
            auto val = srSess->get_item(xpath.c_str());
            srSess->session_switch_ds(SR_DS_RUNNING);
            REQUIRE(!!val);
            REQUIRE(val->data()->get_identityref() == "iana-hardware:sensor"s);
        }
    }
}
