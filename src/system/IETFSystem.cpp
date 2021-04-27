/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <boost/algorithm/string/predicate.hpp>
#include <fstream>
#include "IETFSystem.h"
#include "system_vars.h"
#include "utils/exec.h"
#include "utils/io.h"
#include "utils/log.h"
#include "utils/sysrepo.h"
#include "utils/time.h"

using namespace std::literals;

namespace {

const auto IETF_SYSTEM_MODULE_NAME = "ietf-system"s;
const auto IETF_SYSTEM_STATE_MODULE_PREFIX = "/"s + IETF_SYSTEM_MODULE_NAME + ":system-state/"s;
const auto IETF_SYSTEM_HOSTNAME_PATH = "/ietf-system:system/hostname";
const auto IETF_SYSTEM_STATE_CLOCK_PATH = "/ietf-system:system-state/clock";

/** @brief Returns key=value pairs from (e.g. /etc/os-release) as a std::map */
std::map<std::string, std::string> parseKeyValueFile(const std::filesystem::path& path)
{
    std::map<std::string, std::string> res;
    std::ifstream ifs(path);
    if (!ifs.is_open())
        throw std::invalid_argument("File '" + std::string(path) + "' not found.");

    std::string line;
    while (std::getline(ifs, line)) {
        // man os-release: Lines beginning with "#" shall be ignored as comments. Blank lines are permitted and ignored.
        if (line.empty() || boost::algorithm::starts_with(line, "#")) {
            continue;
        }

        size_t equalSignPos = line.find_first_of('=');
        if (equalSignPos != std::string::npos) {
            std::string key = line.substr(0, equalSignPos);
            std::string val = line.substr(equalSignPos + 1);

            // remove quotes from value
            if (val.length() >= 2 && val.front() == '"' && val.front() == val.back()) {
                val = val.substr(1, val.length() - 2);
            }

            res[key] = val;
        } else { // when there is no = sign, treat the value as empty string
            res[line] = "";
        }
    }

    return res;
}

std::optional<std::string> getHostnameFromChange(const std::shared_ptr<sysrepo::Session> session)
{
    std::optional<std::string> res;

    auto data = session->get_data(IETF_SYSTEM_HOSTNAME_PATH);
    if (data) {
        auto hostnameNode = data->find_path(IETF_SYSTEM_HOSTNAME_PATH)->data().front();
        auto leaf = std::make_shared<libyang::Data_Node_Leaf_List>(hostnameNode);
        res = leaf->value_str();
    }

    return res;
}
}

namespace velia::system {

void IETFSystem::initStaticProperties(const std::filesystem::path& osRelease)
{
    utils::ensureModuleImplemented(m_srSession, IETF_SYSTEM_MODULE_NAME, "2014-08-06");

    std::map<std::string, std::string> osReleaseContents = parseKeyValueFile(osRelease);

    std::map<std::string, std::string> opsSystemStateData {
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-name", osReleaseContents.at("NAME")},
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-release", osReleaseContents.at("VERSION")},
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-version", osReleaseContents.at("VERSION")},
    };

    utils::valuesPush(opsSystemStateData, {}, m_srSession, SR_DS_OPERATIONAL);
}

void IETFSystem::initSystemRestart()
{
    m_srSubscribe->rpc_subscribe(
        ("/" + IETF_SYSTEM_MODULE_NAME + ":system-restart").c_str(),
        [this](::sysrepo::S_Session session, [[maybe_unused]] const char* op_path, [[maybe_unused]] const ::sysrepo::S_Vals input, [[maybe_unused]] sr_event_t event, [[maybe_unused]] uint32_t request_id, [[maybe_unused]] ::sysrepo::S_Vals_Holder output) {
            try {
                velia::utils::execAndWait(m_log, SYSTEMCTL_EXECUTABLE, {"reboot"}, "", {});
            } catch(const std::runtime_error& e) {
                session->set_error("Reboot procedure failed.", nullptr);
                return SR_ERR_OPERATION_FAILED;
            }

            return SR_ERR_OK;
        },
        0,
        SR_SUBSCR_CTX_REUSE);
}

void IETFSystem::initHostname()
{
    sysrepo::ModuleChangeCb hostNameCbRunning = [this] (auto session, auto, auto, auto, auto) {
        if (auto newHostname = getHostnameFromChange(session)) {
            velia::utils::execAndWait(m_log, HOSTNAMECTL_EXECUTABLE, {"set-hostname", *newHostname}, "");
        }
        return SR_ERR_OK;
    };

    sysrepo::ModuleChangeCb hostNameCbStartup = [] (auto session, auto, auto, auto, auto) {
        if (auto newHostname = getHostnameFromChange(session)) {
            utils::safeWriteFile(BACKUP_ETC_HOSTNAME_FILE, *newHostname);
        }
        return SR_ERR_OK;
    };

    sysrepo::OperGetItemsCb hostNameCbOperational = [] (auto session, auto, auto, auto, auto, auto& parent) {
        // + 1 for null-terminating byte, HOST_NAME_MAX doesn't count that
        std::array<char, HOST_NAME_MAX + 1> buffer;

        if (gethostname(buffer.data(), buffer.size()) != 0) {
            throw std::system_error(errno, std::system_category(), "gethostname() failed");
        }

        parent->new_path(
            session->get_context(),
            IETF_SYSTEM_HOSTNAME_PATH,
            buffer.data(),
            LYD_ANYDATA_CONSTSTRING,
            0);

        return SR_ERR_OK;
    };

    m_srSubscribe->module_change_subscribe(IETF_SYSTEM_MODULE_NAME.c_str(), hostNameCbRunning, IETF_SYSTEM_HOSTNAME_PATH, 0, SR_SUBSCR_DONE_ONLY);
    m_srSession->session_switch_ds(SR_DS_STARTUP);
    m_srSubscribe->module_change_subscribe(IETF_SYSTEM_MODULE_NAME.c_str(), hostNameCbStartup, IETF_SYSTEM_HOSTNAME_PATH, 0, SR_SUBSCR_DONE_ONLY);
    m_srSession->session_switch_ds(SR_DS_OPERATIONAL);
    m_srSubscribe->oper_get_items_subscribe(IETF_SYSTEM_MODULE_NAME.c_str(), hostNameCbOperational, IETF_SYSTEM_HOSTNAME_PATH);
}

/** @short Acknowledge writes to dummy fields so that they're visible in the operational DS */
void IETFSystem::initDummies()
{
    m_srSession->session_switch_ds(SR_DS_RUNNING);
    auto ignore = [] (auto, auto, auto, auto, auto) {
        return SR_ERR_OK;
    };
    for (const auto xpath : {"/ietf-system:system/location", "/ietf-system:system/contact"}) {
        m_srSubscribe->module_change_subscribe(IETF_SYSTEM_MODULE_NAME.c_str(), ignore, xpath, 0, SR_SUBSCR_CTX_REUSE | SR_SUBSCR_DONE_ONLY);
    }
}

/** @short Time and clock callbacks */
void IETFSystem::initClock()
{
    m_srSubscribe->oper_get_items_subscribe(IETF_SYSTEM_MODULE_NAME.c_str(),
            [] (auto session, auto, auto, auto, auto, auto& parent) {
                parent->new_path(session->get_context(),
                        (IETF_SYSTEM_STATE_CLOCK_PATH + "/current-datetime"s).c_str(),
                        utils::yangTimeFormat(std::chrono::system_clock::now()).c_str(),
                        LYD_ANYDATA_CONSTSTRING,
                        0);
                return SR_ERR_OK;
            },
            IETF_SYSTEM_STATE_CLOCK_PATH,
            SR_SUBSCR_OPER_MERGE);
}

/** This class handles multiple system properties and publishes them via the ietf-system model:
 * - OS-identification data from osRelease file
 * - Rebooting
 * - Hostname
 */
IETFSystem::IETFSystem(std::shared_ptr<::sysrepo::Session> srSession, const std::filesystem::path& osRelease)
    : m_srSession(std::move(srSession))
    , m_srSubscribe(std::make_shared<::sysrepo::Subscribe>(m_srSession))
    , m_log(spdlog::get("system"))
{
    initStaticProperties(osRelease);
    initSystemRestart();
    initHostname();
    initDummies();
    initClock();
}
}
