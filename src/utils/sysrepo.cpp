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

void valuesToYang(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session, std::shared_ptr<libyang::Data_Node>& parent)
{
    for (const auto& [propertyName, value] : values) {
        spdlog::get("main")->trace("propertyName: {}, value: {}", propertyName, value.c_str());

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

/** @brief Set values into Sysrepo's specified datastore. It changes the datastore and after the data are applied, the original datastore is restored. */
void valuesPush(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session, sr_datastore_t datastore)
{
    sr_datastore_t oldDatastore = session->session_get_ds();
    session->session_switch_ds(datastore);

    valuesPush(values, session);

    session->apply_changes();
    session->session_switch_ds(oldDatastore);
}

/** @brief Set values into Sysrepo's current datastore. */
void valuesPush(const std::map<std::string, std::string>& values, std::shared_ptr<::sysrepo::Session> session)
{
    libyang::S_Data_Node edit;
    valuesToYang(values, session, edit);

    session->edit_batch(edit, "merge");
    session->apply_changes();
}

}
