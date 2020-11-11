#include "trompeloeil_doctest.h"
#include <iterator>
#include "ietf-hardware/IETFHardware.h"
#include "mock/ietf_hardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("HardwareState")
{
    TEST_INIT_LOGS;

    static const auto modulePrefix = "/ietf-hardware-state:hardware"s;

    trompeloeil::sequence seq1;
    auto hwState = std::make_shared<velia::ietf_hardware::IETFHardware>();

    auto fans = std::make_shared<FakeHWMon>();
    auto sysfsTempCpu = std::make_shared<FakeHWMon>(); // thermal_zone hwmon
    auto sysfsTempFront = std::make_shared<FakeHWMon>(); // our addon chip, PCB temperature, front of the chassis
    auto sysfsTempMII0 = std::make_shared<FakeHWMon>(); // Marvell 88E1512, SOM
    auto sysfsTempMII1 = std::make_shared<FakeHWMon>(); // Marvell 88E1512, Clearfog base PCB
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
    hwState->registerComponent(velia::ietf_hardware::component::Roadm("ne", ""));
    hwState->registerComponent(velia::ietf_hardware::component::Controller("ne:ctrl", "ne"));
    hwState->registerComponent(velia::ietf_hardware::component::Fans("ne:fans", "ne", fans, 4));
    hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-front", "ne:ctrl", sysfsTempFront, 1));
    hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-cpu", "ne:ctrl", sysfsTempCpu, 1));
    hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-internal-0", "ne:ctrl", sysfsTempMII0, 1));
    hwState->registerComponent(velia::ietf_hardware::component::SysfsTemperature("ne:ctrl:temperature-internal-1", "ne:ctrl", sysfsTempMII1, 1));
    hwState->registerComponent(velia::ietf_hardware::component::EMMC("ne:ctrl:emmc", "ne:ctrl", emmc));

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
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value", "253"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan1:rpm']/sensor-data/value-type", "rpm"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2']/class", "iana-hardware:fan"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan2:rpm']/sensor-data/value-type", "rpm"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3']/class", "iana-hardware:fan"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/oper-status", "ok"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value", "1280"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-precision", "0"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-scale", "units"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan3:rpm']/sensor-data/value-type", "rpm"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4']/class", "iana-hardware:fan"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4']/parent", "ne:fans"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/class", "iana-hardware:sensor"},
            {"/ietf-hardware-state:hardware/component[name='ne:fans:fan4:rpm']/parent", "ne:fans"},
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
}
