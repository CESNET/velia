#include "trompeloeil_doctest.h"
#include <cstdint>
#include "ietf-hardware/Factory.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"
#include "tests/fs-helpers/utils.h"

using namespace std::literals;

void makeBlankEeprom(const std::filesystem::path& deviceSysfsEntry, const size_t size)
{
    std::ofstream fs{deviceSysfsEntry / "eeprom"};
    std::string buf(size, '\xff');
    fs.write(buf.data(), buf.size());
}

template<typename Container>
auto findByKey(Container& expected, const std::string& needle)
{
    return std::find_if(expected.begin(), expected.end(), [needle](const auto& x) { return x.first == needle; });
}

void filterMissingExpected(const auto& missing, auto& expected)
{
    for (const auto& needle : missing) {
        auto it = findByKey(expected, needle);
        CAPTURE(needle);
        REQUIRE(it != expected.end());
        expected.erase(it);
    }
}

void replaceExpected(auto& expected, const std::string& key, const std::string& value)
{
    auto it = findByKey(expected, key);
    CAPTURE(key);
    REQUIRE(it != expected.end());
    it->second = value;
}

TEST_CASE("factory")
{
    TEST_INIT_LOGS;
    const auto fakeSysfs = std::filesystem::path{CMAKE_CURRENT_BINARY_DIR} / "tests" / "hardware_appliance";
    removeDirectoryTreeIfExists(fakeSysfs);
    std::filesystem::copy(std::filesystem::path{CMAKE_CURRENT_SOURCE_DIR} / "tests" / "ietf-hardware-mock" / "PGCL250333", fakeSysfs, std::filesystem::copy_options::recursive);

    std::size_t count = 0;
    std::vector<std::pair<std::string, std::string>> expected;
    std::vector<std::string> missing;

    SECTION("bidi")
    {
        count = 223;
        expected = {
            {"/component[name='ne']/mfg-date", "2025-01-15T14:15:43-00:00"},
            {"/component[name='ne']/model-name", "sdn-bidi-cplus1572-g2 (PG-CL-SDN_dualBiDi-C-L)"},
            {"/component[name='ne']/serial-num", "PGCL250333"},
            {"/component[name='ne:ctrl']/serial-num", "0910C30854100840143BA080A08000F2"},
            {"/component[name='ne:ctrl:carrier']/mfg-date", "2023-02-23T06:12:51-00:00"},
            {"/component[name='ne:ctrl:carrier']/model-name", "Clearfog Base (SRCFCBE000CV14)"},
            {"/component[name='ne:ctrl:carrier']/serial-num", "IP01195230800010"},
            {"/component[name='ne:ctrl:carrier:console']/serial-num", "DQ00EBGT"},
            {"/component[name='ne:ctrl:carrier:eeprom']/serial-num", "294100B137D2"},
            {"/component[name='ne:ctrl:emmc']/mfg-date", "2022-11-01T00:00:00-00:00"},
            {"/component[name='ne:ctrl:emmc']/serial-num", "0x35c95f36"},
            {"/component[name='ne:ctrl:som']/mfg-date", "2023-02-23T06:12:51-00:00"},
            {"/component[name='ne:ctrl:som']/model-name", "A38x SOM (SRM6828S32D01GE008V21C0)"},
            {"/component[name='ne:ctrl:som']/serial-num", "IP01195230800010"},
            {"/component[name='ne:ctrl:som:eeprom']/serial-num", "80342872BDD7"},
            {"/component[name='ne:fans']/serial-num", "0910C30854100840CC29A088A088009E"},
        };
        SECTION("everything") {
        }
        SECTION("chassis eeprom")
        {
            missing.push_back("/component[name='ne']/mfg-date");
            missing.push_back("/component[name='ne']/model-name");
            missing.push_back("/component[name='ne']/serial-num");
            missing.push_back("/component[name='ne:ctrl:carrier:console']/serial-num");
            const auto device = fakeSysfs / "bus" / "i2c" / "devices" / "1-0053";

            SECTION("missing")
            {
                std::filesystem::remove_all(device);
            }

            SECTION("empty")
            {
                makeBlankEeprom(device, 8192);
            }

            count -= 6;
            filterMissingExpected(missing, expected);
        }
        SECTION("clearfog eeprom")
        {
            missing.push_back("/component[name='ne:ctrl:carrier']/mfg-date");
            missing.push_back("/component[name='ne:ctrl:carrier']/serial-num");
            // missing.push_back("/component[name='ne:ctrl:carrier:eeprom']/serial-num");
            const auto carrierEeprom = fakeSysfs / "bus" / "i2c" / "devices" / "0-0052";

            missing.push_back("/component[name='ne:ctrl:som']/mfg-date");
            missing.push_back("/component[name='ne:ctrl:som']/serial-num");
            // missing.push_back("/component[name='ne:ctrl:som:eeprom']/serial-num");
            const auto somEeprom = fakeSysfs / "bus" / "i2c" / "devices" / "0-0053";

            replaceExpected(expected, "/component[name='ne:ctrl:carrier']/model-name", "ClearFog Base");
            replaceExpected(expected, "/component[name='ne:ctrl:som']/model-name", "ClearFog A388 SOM");

            SECTION("missing")
            {
                missing.push_back("/component[name='ne:ctrl:carrier:eeprom']/serial-num");
                missing.push_back("/component[name='ne:ctrl:som:eeprom']/serial-num");
                count -= 10;

                // we assume that the device tree has been set up properly, and just the actual device failed to probe
                std::filesystem::remove_all(carrierEeprom / "eeprom");
                std::filesystem::remove_all(somEeprom / "eeprom");
            }
            SECTION("empty")
            {
                replaceExpected(expected, "/component[name='ne:ctrl:carrier:eeprom']/serial-num", "FFFFFFFFFFFF");
                replaceExpected(expected, "/component[name='ne:ctrl:som:eeprom']/serial-num", "FFFFFFFFFFFF");
                count -= 8;

                makeBlankEeprom(carrierEeprom, 256);
                makeBlankEeprom(somEeprom, 256);
            }

            filterMissingExpected(missing, expected);
        }
    }

    auto hw = velia::ietf_hardware::createWithoutPower("czechlight-clearfog-g2", fakeSysfs);
    auto conn = sysrepo::Connection{};
    auto sysrepoIETFHardware = velia::ietf_hardware::sysrepo::Sysrepo(conn.sessionStart(), hw, std::chrono::milliseconds{1500});

    // HW polling operates in a background thread, so let's give it some time to start and perform the initial poll
    std::this_thread::sleep_for(333ms);

    auto sess = conn.sessionStart(sysrepo::Datastore::Operational);
    const auto& data = dataFromSysrepo(sess, "/ietf-hardware:hardware");

    for (const auto& [key, value] : expected) {
        CAPTURE(key);
        REQUIRE(data.contains(key));
        REQUIRE(data.at(key) == value);
    }
    for (const auto& key : missing) {
        CAPTURE(key);
        REQUIRE(!data.contains(key));
    }

    REQUIRE(data.size() == count);
}
