#include "trompeloeil_doctest.h"
#include <csignal>
#include <sys/wait.h>
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

/* This is a generic test for the following use-case in the ietf-hardware model
 *  - Process #1 starts and uses sr_set_item to set some data in the "/ietf-hardware:hardware/component" subtree
 *  - Process #2 starts and implements sr_oper_get_items_subscribe for the data in the same subtree
 *  - Process #3 should see all of the data.
 */

#define PIPE_RD 0
#define PIPE_WR 1

/* Communication between signal handler and the rest of the program */
int g_Wakeup[2]; // pipe for wakeup
int g_RecvSignal; // signalled flag

volatile sig_atomic_t g_exit_application = 0;


using namespace std::literals;

static const auto MODULE_NAME = "ietf-hardware"s;
static const auto MODULE_PREFIX = "/"s + MODULE_NAME + ":hardware";

void valuesToYang(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent, const std::string& prefix)
{
    for (const auto& [propertyName, value] : values) {
        if (!parent) {
            parent = std::make_shared<libyang::Data_Node>(
                session->get_context(),
                (prefix + propertyName).c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        } else {
            parent->new_path(
                session->get_context(),
                (prefix + propertyName).c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        }
    }
}

int waitSignalTimeout(int timeout)
{
    struct timeval tv;
    fd_set rd;

    tv.tv_sec = 0;
    tv.tv_usec = timeout;
    FD_ZERO(&rd);
    FD_SET(g_Wakeup[PIPE_RD], &rd);

    select(g_Wakeup[PIPE_RD] + 1, &rd, nullptr, nullptr, &tv);
    return g_RecvSignal;
}

void newSigChild(int)
{
    g_RecvSignal = 1;

    write(g_Wakeup[PIPE_WR], " ", 1);
}


class SysrepoProcess {
public:
    SysrepoProcess(std::map<std::string, std::string> data)
        : m_data(std::move(data))
        , m_killed(false)
    {
    }


    ~SysrepoProcess()
    {
        if (!m_killed) {
            stop();
        }
    }

    void stop()
    {
        kill(m_childPid, SIGTERM);

        int status;
        waitpid(m_childPid, &status, 0);
        m_killed = true;
    }

    void start()
    {
        pipe(m_toParentPipe);

        m_childPid = fork();
        REQUIRE(m_childPid >= 0); // successful fork

        if (m_childPid > 0) { // parent
            close(m_toParentPipe[PIPE_WR]);

            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(m_toParentPipe[PIPE_RD], &rd);
            select(m_toParentPipe[PIPE_RD] + 1, &rd, nullptr, nullptr, nullptr); // do not exit start() before child confirms that sysrepo is initialised to avoid races
        } else {
            close(m_toParentPipe[PIPE_RD]);

            m_srConn = std::make_shared<sysrepo::Connection>();
            m_srSess = std::make_shared<sysrepo::Session>(m_srConn);
            m_srSubs = std::make_shared<sysrepo::Subscribe>(m_srSess);

            childFunc();

            // signal sysrepo initialized to parent
            CHECK(write(m_toParentPipe[PIPE_WR], " ", 1) >= 0);

            // Install sighandler for SIGTERM
            struct sigaction sigact;
            memset(&sigact, 0, sizeof(sigact));
            sigact.sa_handler = [](int) { g_exit_application = 1; };
            sigact.sa_flags = SA_SIGINFO;
            sigaction(SIGTERM, &sigact, nullptr);

            // Block SIGTERM
            sigset_t sigset, oldset;
            sigemptyset(&sigset);
            sigaddset(&sigset, SIGTERM);
            sigprocmask(SIG_BLOCK, &sigset, &oldset);

            while (!g_exit_application) {
                fd_set fd;
                FD_ZERO(&fd);

                // if SIGTERM received at this point, it is deffered until pselect is entered which enables the signal processing again
                pselect(0, &fd, NULL, NULL, NULL, &oldset);
            }

            m_srSubs.reset();
            m_srSess.reset();
            m_srConn.reset();
            exit(1);
        }
    }

private:
    virtual void childFunc() = 0;

protected:
    std::map<std::string, std::string> m_data;
    std::shared_ptr<sysrepo::Connection> m_srConn;
    std::shared_ptr<sysrepo::Session> m_srSess;
    std::shared_ptr<sysrepo::Subscribe> m_srSubs;

private:
    pid_t m_childPid;
    int m_toParentPipe[2];
    bool m_killed;
};

class CallbackSysrepoProcess : public SysrepoProcess {
    using SysrepoProcess::SysrepoProcess;

private:
    void childFunc() override
    {
        sr_log_stderr(SR_LL_DBG);
        m_srSubs->oper_get_items_subscribe(
            MODULE_NAME.c_str(),
            [this](std::shared_ptr<::sysrepo::Session> session, [[maybe_unused]] const char* module_name, [[maybe_unused]] const char* xpath, [[maybe_unused]] const char* request_xpath, uint32_t request_id, std::shared_ptr<libyang::Data_Node>& parent) {
                if (m_srLastRequestId == request_id) {
                    return SR_ERR_OK;
                }
                m_srLastRequestId = request_id;

                valuesToYang(m_data, session, parent, MODULE_PREFIX);
                return SR_ERR_OK;
            },
            (MODULE_PREFIX + "/*").c_str(),
            SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
    }

    uint32_t m_srLastRequestId = 0;
};

class SetItemSysrepoProcess : public SysrepoProcess {
    using SysrepoProcess::SysrepoProcess;

private:
    void childFunc() override
    {
        sr_log_stderr(SR_LL_DBG);
        m_srSess->session_switch_ds(SR_DS_OPERATIONAL);
        for (const auto& [k, v] : m_data) {
            m_srSess->set_item_str((MODULE_PREFIX + k).c_str(), v.c_str());
        }
        m_srSess->apply_changes();
        m_srSess->session_switch_ds(SR_DS_RUNNING);
    }
};

TEST_CASE("HardwareState")
{
    std::map<std::string, std::string> process1Data {
        {"/component[name='ne']/class", "iana-hardware:module"},
        {"/component[name='ne:edfa']/class", "iana-hardware:module"},
    };

    std::map<std::string, std::string> process2Data {
        {"/component[name='ne']/description", "This data was brought to you by process 2 (subscr)."},
        {"/component[name='ne:ctrl']/class", "iana-hardware:module"},
    };

    SECTION("Test when both processes are running")
    {
        {
            SetItemSysrepoProcess p1(process1Data);
            CallbackSysrepoProcess p2(process2Data);

            p1.start();
            p2.start();

            TEST_SYSREPO_INIT;
            TEST_SYSREPO_INIT_LOGS;

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

        TEST_SYSREPO_INIT;
        srSess->session_switch_ds(SR_DS_OPERATIONAL);
        REQUIRE(srSess->get_items("/ietf-hardware:hardware//*") == nullptr);
    }

    SECTION("Test when one terminates")
    {
        SetItemSysrepoProcess p1(process1Data);
        CallbackSysrepoProcess p2(process2Data);

        p1.start();
        p2.start();

        TEST_SYSREPO_INIT;
        TEST_SYSREPO_INIT_LOGS;

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

        p1.stop();

        srSess->session_switch_ds(SR_DS_OPERATIONAL);
        REQUIRE(dataFromSysrepo(srSess, "/ietf-hardware:hardware") == std::map<std::string, std::string> {
                    {"/component[name='ne']", ""},
                    {"/component[name='ne']/name", "ne"},
                    {"/component[name='ne']/description", "This data was brought to you by process 2 (subscr)."},
                    {"/component[name='ne']/sensor-data", ""},
                    {"/component[name='ne:ctrl']", ""},
                    {"/component[name='ne:ctrl']/name", "ne:ctrl"},
                    {"/component[name='ne:ctrl']/class", "iana-hardware:module"},
                    {"/component[name='ne:ctrl']/sensor-data", ""},
                });
        srSess->session_switch_ds(SR_DS_RUNNING);
    }
}
