#include <csignal>
#include <cstring>
#include <fstream>
#include <map>
#include <sysrepo-cpp/Session.hpp>
#include <unistd.h>

using namespace std::string_literals;

volatile sig_atomic_t g_exit_application = 0;

static const std::string MODULE_NAME = "ietf-hardware";
static const std::string MODULE_PREFIX = "/" + MODULE_NAME + ":hardware";

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

void usage(const char* progName)
{
    std::cout << "Usage: " << progName << "--subscribe|--setitem" << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    bool isDaemonSubscribe = argv[1] == "--subscribe"s;
    bool isDaemonSetItem = argv[1] == "--set-item"s;

    if (isDaemonSubscribe == isDaemonSetItem) {
        usage(argv[0]);
        return 1;
    }

    auto srConn = std::make_shared<sysrepo::Connection>();
    auto srSess = std::make_shared<sysrepo::Session>(srConn);

    std::shared_ptr<sysrepo::Subscribe> srSubs;
    uint32_t srLastRequestId = 0; // for subscribe part

    std::map<std::string, std::string> data;

    if (isDaemonSubscribe) {
        data = {
            {"/component[name='ne']/description", "This data was brought to you by process 2 (subscr)."},
            {"/component[name='ne:ctrl']/class", "iana-hardware:module"},
        };

        srSubs = std::make_shared<sysrepo::Subscribe>(srSess);

        srSubs->oper_get_items_subscribe(
            MODULE_NAME.c_str(),
            [&](std::shared_ptr<::sysrepo::Session> session, [[maybe_unused]] const char* module_name, [[maybe_unused]] const char* xpath, [[maybe_unused]] const char* request_xpath, uint32_t request_id, std::shared_ptr<libyang::Data_Node>& parent) {
                if (srLastRequestId == request_id) {
                    return SR_ERR_OK;
                }
                srLastRequestId = request_id;

                valuesToYang(data, session, parent, MODULE_PREFIX);
                return SR_ERR_OK;
            },
            (MODULE_PREFIX + "/*").c_str(),
            SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
    } else if (isDaemonSetItem) {
        data = {
            {"/component[name='ne']/class", "iana-hardware:module"},
            {"/component[name='ne:edfa']/class", "iana-hardware:module"},
        };

        srSess->session_switch_ds(SR_DS_OPERATIONAL);
        for (const auto& [k, v] : data) {
            srSess->set_item_str((MODULE_PREFIX + k).c_str(), v.c_str());
        }
        srSess->apply_changes();
        srSess->session_switch_ds(SR_DS_RUNNING);
    }

    // touch a file so somebody can read that sysrepo things are initialised
    {
        std::string filename = std::to_string(getpid()) + ".sysrepo";
        std::ofstream ofs(filename);
        ofs << "";
    }

    sleep(1000); // I guess, this is plenty of seconds, right?

    return 0;
}
