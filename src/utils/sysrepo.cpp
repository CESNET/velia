/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "sysrepo.h"
#include "utils/log.h"

extern "C" {
#include <sysrepo.h>
}

extern "C" {
/** @short Propagate sysrepo events to spdlog */
static void spdlog_sr_log_cb(sr_log_level_t level, const char* message)
{
    // Thread safety note: this is, as far as I know, thread safe:
    // - the static initialization itself is OK
    // - all loggers which we instantiate are thread-safe
    // - std::shared_ptr::operator-> is const, and all const members of that class are documented to be thread-safe
    static auto log = spdlog::get("sysrepo");
    assert(log);
    switch (level) {
    case SR_LL_NONE:
    case SR_LL_ERR:
        log->error(message);
        break;
    case SR_LL_WRN:
        log->warn(message);
        break;
    case SR_LL_INF:
        log->info(message);
        break;
    case SR_LL_DBG:
        log->debug(message);
        break;
    }
}
}

namespace velia::utils {

/** @short Setup sysrepo log forwarding
You must call cla::utils::initLogs prior to this function.
*/
void initLogsSysrepo()
{
    sr_log_set_cb(spdlog_sr_log_cb);
}

void valuesToYang(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent)
{
    auto netconf = session->get_context()->get_module("ietf-netconf");
    auto log = spdlog::get("main");

    for (const auto& propertyName : removePaths) {
        log->trace("Processing node deletion {}", propertyName);

        if (!parent) {
            parent = std::make_shared<libyang::Data_Node>(
                session->get_context(),
                propertyName.c_str(),
                nullptr,
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_EDIT);
        } else {
            parent->new_path(
                session->get_context(),
                propertyName.c_str(),
                nullptr,
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_EDIT);
        }

        auto deletion = parent->find_path(propertyName.c_str());
        if (deletion->number() != 1) {
            throw std::logic_error {"Cannot find XPath " + propertyName + " for deletion in libyang's new_path() output"};
        }
        deletion->data()[0]->insert_attr(netconf, "operation", "remove");
    }

    for (const auto& [propertyName, value] : values) {
        log->trace("Processing node update {} -> {}", propertyName, value);

        if (!parent) {
            parent = std::make_shared<libyang::Data_Node>(
                session->get_context(),
                propertyName.c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        } else {
            parent->new_path(
                session->get_context(),
                propertyName.c_str(),
                value.c_str(),
                LYD_ANYDATA_CONSTSTRING,
                LYD_PATH_OPT_OUTPUT);
        }
    }
}

/** @brief Set or remove values in Sysrepo's specified datastore. It changes the datastore and after the data are applied, the original datastore is restored. */
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session, sr_datastore_t datastore)
{
    sr_datastore_t oldDatastore = session->session_get_ds();
    session->session_switch_ds(datastore);

    valuesPush(values, removePaths, session);

    session->apply_changes();
    session->session_switch_ds(oldDatastore);
}

/** @brief Set or remove paths in Sysrepo's current datastore. */
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::shared_ptr<::sysrepo::Session> session)
{
    libyang::S_Data_Node edit;
    valuesToYang(values, removePaths, session, edit);

    session->edit_batch(edit, "merge");
    session->apply_changes();
}

/** @brief Checks whether a module is implemented in Sysrepo and throws if not. */
void ensureModuleImplemented(std::shared_ptr<::sysrepo::Session> session, const std::string& module, const std::string& revision)
{
    if (auto mod = session->get_context()->get_module(module.c_str(), revision.c_str()); !mod || !mod->implemented()) {
        throw std::runtime_error(module + "@" + revision + " is not implemented in sysrepo.");
    }
}
}
