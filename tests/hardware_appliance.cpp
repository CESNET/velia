#include "trompeloeil_doctest.h"
#include <cstdint>
#include "ietf-hardware/Factory.h"
#include "ietf-hardware/IETFHardware.h"
#include "ietf-hardware/sysrepo/Sysrepo.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("factory")
{
    TEST_INIT_LOGS;

    auto hw = velia::ietf_hardware::createWithoutPower(
        "czechlight-clearfog-g2",
        // FIXME: make this dynamic and test unplugging as well
        std::filesystem::path{CMAKE_CURRENT_SOURCE_DIR} / "tests" / "ietf-hardware-mock" / "PGCL250333");
    auto conn = sysrepo::Connection{};
    auto sysrepoIETFHardware = velia::ietf_hardware::sysrepo::Sysrepo(conn.sessionStart(), hw, std::chrono::milliseconds{1500});

    // HW polling operates in a background thread, so let's give it some time to start and perform the initial poll
    std::this_thread::sleep_for(333ms);

    auto sess = conn.sessionStart(sysrepo::Datastore::Operational);
    const auto& data = dataFromSysrepo(sess, "/ietf-hardware:hardware");

    REQUIRE(data.at("/component[name='ne']/mfg-date") == "2025-01-15T14:15:43-00:00");
    REQUIRE(data.at("/component[name='ne']/model-name") == "sdn-bidi-cplus1572-g2 (PG-CL-SDN_dualBiDi-C-L)");
    REQUIRE(data.at("/component[name='ne']/serial-num") == "PGCL250333");
    REQUIRE(data.at("/component[name='ne:ctrl']/serial-num") == "0910C30854100840143BA080A08000F2");
    REQUIRE(data.at("/component[name='ne:ctrl:carrier']/mfg-date") == "2023-02-23T06:12:51-00:00");
    REQUIRE(data.at("/component[name='ne:ctrl:carrier']/model-name") == "Clearfog Base (SRCFCBE000CV14)");
    REQUIRE(data.at("/component[name='ne:ctrl:carrier']/serial-num") == "IP01195230800010");
    REQUIRE(data.at("/component[name='ne:ctrl:carrier:console']/serial-num") == "DQ00EBGT");
    REQUIRE(data.at("/component[name='ne:ctrl:carrier:eeprom']/serial-num") == "294100B137D2");
    REQUIRE(data.at("/component[name='ne:ctrl:emmc']/mfg-date") == "2022-11-01T00:00:00-00:00");
    REQUIRE(data.at("/component[name='ne:ctrl:emmc']/serial-num") == "0x35c95f36");
    REQUIRE(data.at("/component[name='ne:ctrl:som']/mfg-date") == "2023-02-23T06:12:51-00:00");
    REQUIRE(data.at("/component[name='ne:ctrl:som']/model-name") == "A38x SOM (SRM6828S32D01GE008V21C0)");
    REQUIRE(data.at("/component[name='ne:ctrl:som']/serial-num") == "IP01195230800010");
    REQUIRE(data.at("/component[name='ne:fans']/serial-num") == "0910C30854100840CC29A088A088009E");
    REQUIRE(data.size() == 223);
}
