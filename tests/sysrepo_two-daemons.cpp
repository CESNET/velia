#include "trompeloeil_doctest.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

/* This is a generic test for the following use-case in the ietf-hardware model
 *  - Process #1 starts and uses sr_set_item to set some data in the "/ietf-hardware:hardware/component" subtree
 *  - Process #2 starts and implements sr_oper_get_items_subscribe for the data in the same subtree
 *  - Process #3 should see all of the data.
 *
 *  Processes #1 and #2 are started (and stopped) by ctest wrapper script (sysrepo_test_merge_fixture.sh) and their code can be found in sysrepo_test_merge_daemon.cpp
 *  The wrapper script ŕeturns *after* both processes report that sysrepo is initialised (ie., callback is added in #2, items are set in #1).
 *  This is implemented simply via some checks whether file exists (see the sh file).
 */

using namespace std::chrono_literals;

TEST_CASE("HardwareState with two daemons")
{
    TEST_SYSREPO_INIT;
    TEST_SYSREPO_INIT_LOGS;

    SECTION("Test when both processes are running")
    {
        srSess->session_switch_ds(SR_DS_OPERATIONAL);
        REQUIRE(dataFromSysrepo(srSess, "/ietf-hardware:hardware") == std::map<std::string, std::string> {
                    {"/component[name='ne']", ""},
                    {"/component[name='ne']/name", "ne"},
                    {"/component[name='ne']/class", "iana-hardware:module"},
                    {"/component[name='ne']/description", "This data was brought to you by process 2 (subscr)."},
                    {"/component[name='ne']/sensor-data", ""},
                    {"/component[name='ne:edfa']", ""},
                    {"/component[name='ne:edfa']/name", "ne:edfa"},
                    {"/component[name='ne:edfa']/class", "iana-hardware:module"},
                    {"/component[name='ne:edfa']/sensor-data", ""},
                    {"/component[name='ne:ctrl']", ""},
                    {"/component[name='ne:ctrl']/name", "ne:ctrl"},
                    {"/component[name='ne:ctrl']/class", "iana-hardware:module"},
                    {"/component[name='ne:ctrl']/sensor-data", ""},
                });
        srSess->session_switch_ds(SR_DS_RUNNING);
    }
}
