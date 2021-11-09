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

namespace {
std::vector<velia::utils::YANGPair> mapToVector(const std::map<std::string, std::string>& values)
{
    std::vector<velia::utils::YANGPair> res;
    for (const auto& [xpath, value] : values) {
        res.emplace_back(xpath, value);
    }

    return res;
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

void valuesToYang(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent)
{
    valuesToYang(mapToVector(values), removePaths, std::move(session), parent);
}

void valuesToYang(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent)
{
    auto netconf = session.getContext().getModuleImplemented("ietf-netconf");
    auto log = spdlog::get("main");

    for (const auto& propertyName : removePaths) {
        log->trace("Processing node deletion {}", propertyName);

        if (!parent) {
            parent = session.getContext().newPath(propertyName.c_str(), nullptr, libyang::CreationOptions::Opaque);
        } else {
            parent->newPath(propertyName.c_str(), nullptr, libyang::CreationOptions::Opaque);
        }

        auto deletion = parent->findPath(propertyName.c_str());
        if (!deletion) {
            throw std::logic_error {"Cannot find XPath " + propertyName + " for deletion in libyang's new_path() output"};
        }
        deletion->newMeta(*netconf, "operation", "remove");
    }

    for (const auto& [propertyName, value] : values) {
        log->trace("Processing node update {} -> {}", propertyName, value);

        if (!parent) {
            parent = session.getContext().newPath(propertyName.c_str(), value.c_str(), libyang::CreationOptions::Output);
        } else {
            parent->newPath(propertyName.c_str(), value.c_str(), libyang::CreationOptions::Output);
        }
    }
}

/** @brief Set or remove values in Sysrepo's specified datastore. It changes the datastore and after the data are applied, the original datastore is restored. */
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session, sysrepo::Datastore datastore)
{
    auto oldDatastore = session.activeDatastore();
    session.switchDatastore(datastore);

    valuesPush(values, removePaths, session);

    session.switchDatastore(oldDatastore);
}

/** @brief Set or remove paths in Sysrepo's current datastore. */
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, ::sysrepo::Session session)
{
    if (values.empty() && removePaths.empty()) return;

    std::optional<libyang::DataNode> edit;
    valuesToYang(values, removePaths, session, edit);

    if (edit) {
        session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
        session.applyChanges();
    }
}

/** @brief Set or remove values in Sysrepo's specified datastore. It changes the datastore and after the data are applied, the original datastore is restored. */
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, sysrepo::Session session, sysrepo::Datastore datastore)
{
    auto oldDatastore = session.activeDatastore();
    session.switchDatastore(datastore);

    valuesPush(values, removePaths, session);

    session.switchDatastore(oldDatastore);
}

/** @brief Set or remove paths in Sysrepo's current datastore. */
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, sysrepo::Session session)
{
    if (values.empty() && removePaths.empty())
        return;

    std::optional<libyang::DataNode> edit;
    valuesToYang(values, removePaths, session, edit);

    if (edit) {
    session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
    session.applyChanges();
    }
}

/** @brief Checks whether a module is implemented in Sysrepo and throws if not. */
void ensureModuleImplemented(::sysrepo::Session session, const std::string& module, const std::string& revision)
{
    if (auto mod = session.getContext().getModule(module.c_str(), revision.c_str()); !mod || !mod->implemented()) {
        throw std::runtime_error(module + "@" + revision + " is not implemented in sysrepo.");
    }
}
YANGPair::YANGPair(std::string xpath, std::string value)
    : m_xpath(std::move(xpath))
    , m_value(std::move(value))
{
}
}
