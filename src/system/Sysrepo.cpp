/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <boost/algorithm/string/predicate.hpp>
#include <fstream>
#include "Sysrepo.h"
#include "utils/io.h"
#include "utils/log.h"

using namespace std::literals;

namespace {

const auto IETF_SYSTEM_MODULE_NAME = "ietf-system"s;
const auto IETF_SYSTEM_STATE_MODULE_PREFIX = "/"s + IETF_SYSTEM_MODULE_NAME + ":system-state/"s;

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

}

namespace velia::system {

/** @brief Reads some OS-identification data from osRelease file and publishes them via ietf-system model */
Sysrepo::Sysrepo(std::shared_ptr<::sysrepo::Connection> srConn, const std::filesystem::path& osRelease, std::shared_ptr<RAUC> rauc)
    : m_srConn(std::move(srConn))
    , m_srSession(std::make_shared<::sysrepo::Session>(m_srConn))
    , m_srSubscribe(std::make_shared<::sysrepo::Subscribe>(m_srSession))
    , m_rauc(std::move(rauc))
    , m_log(spdlog::get("system"))
{
    std::map<std::string, std::string> osReleaseContents = parseKeyValueFile(osRelease);

    std::map<std::string, std::string> opsSystemStateData {
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-name", osReleaseContents.at("NAME")},
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-release", osReleaseContents.at("VERSION")},
        {IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/os-version", osReleaseContents.at("VERSION")},
    };

    sr_datastore_t oldDatastore = m_srSession->session_get_ds();
    m_srSession->session_switch_ds(SR_DS_OPERATIONAL);

    for (const auto& [k, v] : opsSystemStateData) {
        m_log->debug("Pushing to sysrepo: {} = {}", k, v);
        m_srSession->set_item_str(k.c_str(), v.c_str());
    }

    m_srSession->apply_changes();
    m_srSession->session_switch_ds(oldDatastore);

    auto notify = std::make_shared<RAUC::InstallNotifier>([](int32_t, const std::string&, int32_t) {}, [this](int32_t returnValue, const std::string& lastError) {
        // new session because this function is executed from another thread
        auto srSess = std::make_shared<::sysrepo::Session>(m_srConn, SR_DS_OPERATIONAL);
        srSess->set_item_str("/czechlight-system:rauc/installation/in-progress", "false");
        srSess->set_item_str("/czechlight-system:rauc/installation/return-value", std::to_string(returnValue).c_str());
        if (!lastError.empty()) {
            srSess->set_item_str("/czechlight-system:rauc/installation/last-error", lastError.c_str());
        }
        srSess->apply_changes(); });

    m_srSubscribe->rpc_subscribe(
        "/czechlight-system:rauc-install",
        [this, notify](sysrepo::S_Session session, [[maybe_unused]] const char* op_path, const sysrepo::S_Vals input, [[maybe_unused]] sr_event_t event, [[maybe_unused]] uint32_t request_id, sysrepo::S_Vals_Holder output) {
            std::string source = input->val(0)->val_to_string();

            sr_datastore_t oldDatastore = session->session_get_ds();
            session->session_switch_ds(SR_DS_OPERATIONAL);
            session->delete_item("/czechlight-system:rauc/installation/return-value");
            session->delete_item("/czechlight-system:rauc/installation/last-error");
            session->set_item_str("/czechlight-system:rauc/installation/in-progress", "true");
            session->apply_changes();
            session->session_switch_ds(oldDatastore);

            try {
                m_rauc->install(source, notify);
                auto vals = output->allocate(1);
                vals->val(0)->set("/czechlight-system:rauc-install/status", "Installing");
            } catch (std::logic_error& e) {
                m_log->warn("RAUC install error '{}'", e.what());

                auto vals = output->allocate(1);
                vals->val(0)->set("/czechlight-system:rauc-install/status", e.what());
            }

            return SR_ERR_OK;
        },
        0,
        SR_SUBSCR_CTX_REUSE);
}
}
