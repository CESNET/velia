#include "trompeloeil_doctest.h"
#include "dbus-helpers/dbus_rauc_server.h"
#include "mock/system.h"
#include "pretty_printers.h"
#include "system/RAUC.h"
#include "test_log_setup.h"

TEST_CASE("Fetch RAUC data over DBus")
{
    using namespace std::literals::chrono_literals;

    TEST_INIT_LOGS;
    trompeloeil::sequence seq1;

    // setup separate connections for both client and server. Can be done using one connection only but this way it is more generic
    auto serverConnection = sdbus::createSessionBusConnection("de.pengutronix.rauc");
    auto clientConnection = sdbus::createSessionBusConnection();

    // enter client and servers event loops
    clientConnection->enterEventLoopAsync();
    serverConnection->enterEventLoopAsync();

    std::string primarySlot = "rootfs.1";
    std::map<std::string, velia::system::RAUC::SlotProperties> status = {
        {"rootfs.1", {
                         {"activated.count", uint32_t {39}},
                         {"activated.timestamp", "2021-01-13T17:20:18Z"},
                         {"bootname", "B"},
                         {"boot-status", "good"},
                         {"bundle.compatible", "czechlight-clearfog"},
                         {"bundle.version", "v4-103-g34d2f48"},
                         {"class", "rootfs"},
                         {"device", "/dev/mmcblk0p3"},
                         {"installed.count", uint32_t {39}},
                         {"installed.timestamp", "2021-01-13T17:20:15Z"},
                         {"mountpoint", "/"},
                         {"sha256", "07b30d065c7aad64d2006ce99fd339c929d3ca97b666fca4584b9ef726469fc4"},
                         {"size", uint64_t {45601892}},
                         {"state", "booted"},
                         {"status", "ok"},
                         {"type", "ext4"},
                     }},
        {"rootfs.0", {
                         {"activated.count", uint32_t {41}},
                         {"activated.timestamp", "2021-01-13T17:15:54Z"},
                         {"bootname", "A"},
                         {"boot-status", "bad"},
                         {"bundle.compatible", "czechlight-clearfog"},
                         {"bundle.version", "v4-104-ge80fcd4"},
                         {"class", "rootfs"},
                         {"device", "/dev/mmcblk0p1"},
                         {"installed.count", uint32_t {41}},
                         {"installed.timestamp", "2021-01-13T17:15:50Z"},
                         {"sha256", "6d81e8f341edd17c127811f7347c7e23d18c2fc25c0bdc29ac56999cc9c25629"},
                         {"size", uint64_t {45647664}},
                         {"state", "inactive"},
                         {"status", "ok"},
                         {"type", "ext4"},
                     }},
        {"cfg.1", {
                      {"bundle.compatible", "czechlight-clearfog"},
                      {"bundle.version", "v4-103-g34d2f48"},
                      {"class", "cfg"},
                      {"device", "/dev/mmcblk0p4"},
                      {"installed.count", uint32_t {39}},
                      {"installed.timestamp", "2021-01-13T17:20:18Z"},
                      {"mountpoint", "/cfg"},
                      {"parent", "rootfs.1"},
                      {"sha256", "5ca1b6c461fc194055d52b181f57c63dc1d34c19d041f6395e6f6abc039692bb"},
                      {"size", uint64_t {108}},
                      {"state", "active"},
                      {"status", "ok"},
                      {"type", "ext4"},
                  }},
        {"cfg.0", {
                      {"bundle.compatible", "czechlight-clearfog"},
                      {"bundle.version", "v4-104-ge80fcd4"},
                      {"class", "cfg"},
                      {"device", "/dev/mmcblk0p2"},
                      {"installed.count", uint32_t {41}},
                      {"installed.timestamp", "2021-01-13T17:15:54Z"},
                      {"parent", "rootfs.0"},
                      {"sha256", "5ca1b6c461fc194055d52b181f57c63dc1d34c19d041f6395e6f6abc039692bb"},
                      {"size", uint64_t {108}},
                      {"state", "inactive"},
                      {"status", "ok"},
                      {"type", "ext4"},
                  }},
    };

    auto server = DBusRAUCServer(*serverConnection, primarySlot, status);
    auto rauc = std::make_shared<velia::system::RAUC>(*clientConnection);

    SECTION("Test slot status")
    {
        REQUIRE(rauc->primarySlot() == primarySlot);
        REQUIRE(rauc->slotStatus() == status);
    }

    SECTION("Install bundle")
    {
        auto fake = std::make_shared<FakeRAUCInstallNotifier>();

        SECTION("Installation OK")
        {
            server.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::OK); // Not cool but I don't feel like I should be creating some abstractions here in tests.

            FAKE_RAUC_PROGRESS(fake, 0, "Installing", 1);
            FAKE_RAUC_PROGRESS(fake, 0, "Determining slot states", 2);
            FAKE_RAUC_PROGRESS(fake, 20, "Determining slot states done.", 2);
            FAKE_RAUC_PROGRESS(fake, 20, "Checking bundle", 2);
            FAKE_RAUC_PROGRESS(fake, 20, "Veryfing signature", 3);
            FAKE_RAUC_PROGRESS(fake, 40, "Veryfing signature done.", 3);
            FAKE_RAUC_PROGRESS(fake, 40, "Checking bundle done.", 2);
            FAKE_RAUC_PROGRESS(fake, 40, "Loading manifest file", 2);
            FAKE_RAUC_PROGRESS(fake, 60, "Loading manifest file done.", 2);
            FAKE_RAUC_PROGRESS(fake, 60, "Determining target install group", 2);
            FAKE_RAUC_PROGRESS(fake, 80, "Determining target install group done.", 2);
            FAKE_RAUC_PROGRESS(fake, 80, "Updating slots", 2);
            FAKE_RAUC_PROGRESS(fake, 80, "Checking slot rootfs.0", 3);
            FAKE_RAUC_PROGRESS(fake, 85, "Checking slot rootfs.0 done.", 3);
            FAKE_RAUC_PROGRESS(fake, 85, "Copying image to rootfs.0", 3);
            FAKE_RAUC_PROGRESS(fake, 90, "Copying image to rootfs.0 done.", 3);
            FAKE_RAUC_PROGRESS(fake, 90, "Checking slot cfg.0", 3);
            FAKE_RAUC_PROGRESS(fake, 95, "Checking slot cfg.0 done.", 3);
            FAKE_RAUC_PROGRESS(fake, 95, "Copying image to cfg.0", 3);
            FAKE_RAUC_PROGRESS(fake, 100, "Copying image to cfg.0 done.", 3);
            FAKE_RAUC_PROGRESS(fake, 100, "Updating slots done.", 2);
            FAKE_RAUC_PROGRESS(fake, 100, "Installing done.", 1);
            FAKE_RAUC_COMPLETED(fake, 0, "");

            rauc->install("/path/to/bundle", fake);
            waitForCompletionAndBitMore(seq1);
        }

        SECTION("Installation failure")
        {
            server.installBundleBehaviour(DBusRAUCServer::InstallBehaviour::FAILURE);

            FAKE_RAUC_PROGRESS(fake, 0, "Installing", 1);
            FAKE_RAUC_PROGRESS(fake, 0, "Determining slot states", 2);
            FAKE_RAUC_PROGRESS(fake, 20, "Determining slot states done.", 2);
            FAKE_RAUC_PROGRESS(fake, 20, "Checking bundle", 2);
            FAKE_RAUC_PROGRESS(fake, 40, "Checking bundle failed.", 2);
            FAKE_RAUC_PROGRESS(fake, 100, "Installing failed.", 1);
            FAKE_RAUC_COMPLETED(fake, 1, "Failed to download bundle https://10.88.3.11:8000/update.raucb: Transfer failed: error:1408F10B:SSL routines:ssl3_get_record:wrong version number");

            rauc->install("/path/to/bundle", fake);
            waitForCompletionAndBitMore(seq1);
        }

        SECTION("Invoking another installation before the first ends")
        {
            // We don't know how much the first install does
            FAKE_RAUC_PROGRESS(fake, 0, "Installing", 1).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 0, "Determining slot states", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 20, "Determining slot states done.", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 20, "Checking bundle", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 20, "Veryfing signature", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 40, "Veryfing signature done.", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 40, "Checking bundle done.", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 40, "Loading manifest file", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 60, "Loading manifest file done.", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 60, "Determining target install group", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 80, "Determining target install group done.", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 80, "Updating slots", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 80, "Checking slot rootfs.0", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 85, "Checking slot rootfs.0 done.", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 85, "Copying image to rootfs.0", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 90, "Copying image to rootfs.0 done.", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 90, "Checking slot cfg.0", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 95, "Checking slot cfg.0 done.", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 95, "Copying image to cfg.0", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 100, "Copying image to cfg.0 done.", 3).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 100, "Updating slots done.", 2).TIMES(AT_MOST(1));
            FAKE_RAUC_PROGRESS(fake, 100, "Installing done.", 1).TIMES(AT_MOST(1));

            rauc->install("/path/to/bundle", fake);
            REQUIRE_THROWS_AS(rauc->install("/path/to/bundle", fake), sdbus::Error);
        }
    }
}
