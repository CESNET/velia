/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "sysrepo.h"
#include "utils/benchmark.h"
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

void valuesToYang(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent)
{
    valuesToYang(mapToVector(values), removePaths, discardPaths, std::move(session), parent);
}

void valuesToYang(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent)
{
    auto netconf = session.getContext().getModuleImplemented("ietf-netconf");
    auto log = spdlog::get("main");

    for (const auto& propertyName : removePaths) {
        if (!parent) {
            parent = session.getContext().newPath(propertyName, std::nullopt, libyang::CreationOptions::Opaque);
        } else {
            parent->newPath(propertyName, std::nullopt, libyang::CreationOptions::Opaque);
        }

        auto deletion = parent->findPath(propertyName);
        if (!deletion) {
            throw std::logic_error {"Cannot find XPath " + propertyName + " for deletion in libyang's new_path() output"};
        }
        deletion->newMeta(*netconf, "operation", "remove");
    }

    for (const auto& [propertyName, value] : values) {
        if (!parent) {
            parent = session.getContext().newPath(propertyName, value, libyang::CreationOptions::Output);
        } else {
            parent->newPath(propertyName, value, libyang::CreationOptions::Output);
        }
    }

    for (const auto& propertyName : discardPaths) {
        auto discard = session.getContext().newOpaqueJSON("sysrepo", "discard-items", libyang::JSON{propertyName});

        if (!parent) {
            parent = discard;
        } else {
            parent->insertSibling(*discard);
            parent->newPath(propertyName, std::nullopt, libyang::CreationOptions::Opaque);
        }
    }
}

/** @brief Set or remove values in Sysrepo's specified datastore. It changes the datastore and after the data are applied, the original datastore is restored. */
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session, sysrepo::Datastore datastore)
{
    ScopedDatastoreSwitch s(session, datastore);
    valuesPush(values, removePaths, discardPaths, session);
}

/** @brief Set or remove paths in Sysrepo's current datastore. */
void valuesPush(const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, ::sysrepo::Session session)
{
    WITH_TIME_MEASUREMENT{};
    if (values.empty() && removePaths.empty() && discardPaths.empty()) return;

    std::optional<libyang::DataNode> edit;
    valuesToYang(values, removePaths, discardPaths, session, edit);

    if (edit) {
        session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
        spdlog::get("main")->trace("valuesPush: {}", *session.getPendingChanges()->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
        session.applyChanges();
    }
}

/** @brief Set or remove values in Sysrepo's specified datastore. It changes the datastore and after the data are applied, the original datastore is restored. */
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, sysrepo::Session session, sysrepo::Datastore datastore)
{
    ScopedDatastoreSwitch s(session, datastore);
    valuesPush(values, removePaths, discardPaths, session);
}

/** @brief Set or remove paths in Sysrepo's current datastore. */
void valuesPush(const std::vector<YANGPair>& values, const std::vector<std::string>& removePaths, const std::vector<std::string>& discardPaths, sysrepo::Session session)
{
    WITH_TIME_MEASUREMENT{};
    if (values.empty() && removePaths.empty() && discardPaths.empty())
        return;

    std::optional<libyang::DataNode> edit;
    valuesToYang(values, removePaths, discardPaths, session, edit);

    if (edit) {
        session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
        spdlog::get("main")->trace("valuesPush: {}", *session.getPendingChanges()->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
        session.applyChanges();
    }
}

/** @brief Checks whether a module is implemented in Sysrepo and throws if not. */
void ensureModuleImplemented(::sysrepo::Session session, const std::string& module, const std::string& revision)
{
    if (auto mod = session.getContext().getModule(module, revision); !mod || !mod->implemented()) {
        throw std::runtime_error(module + "@" + revision + " is not implemented in sysrepo.");
    }
}
YANGPair::YANGPair(std::string xpath, std::string value)
    : m_xpath(std::move(xpath))
    , m_value(std::move(value))
{
}

void setErrors(::sysrepo::Session session, const std::string& msg)
{
    session.setNetconfError(sysrepo::NetconfErrorInfo{
        .type = "application",
        .tag = "operation-failed",
        .appTag = {},
        .path = {},
        .message = msg,
        .infoElements = {},
    });
    session.setErrorMessage(msg);
}

ScopedDatastoreSwitch::ScopedDatastoreSwitch(sysrepo::Session session, sysrepo::Datastore ds)
    : m_session(std::move(session))
    , m_oldDatastore(m_session.activeDatastore())
{
    m_session.switchDatastore(ds);
}

ScopedDatastoreSwitch::~ScopedDatastoreSwitch()
{
    m_session.switchDatastore(m_oldDatastore);
}
}
