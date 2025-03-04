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

namespace velia::utils {

/** @short Setup sysrepo log forwarding
You must call cla::utils::initLogs prior to this function.
*/
void initLogsSysrepo()
{
    sr_log_set_cb(spdlog_sr_log_cb);
}

void valuesToYang(const YANGData& values, const std::vector<std::string>& foreignRemovals, const std::vector<std::string>& ourRemovals, ::sysrepo::Session session, std::optional<libyang::DataNode>& parent)
{
    auto log = spdlog::get("main");

    for (const auto& xpath : foreignRemovals) {
        // FIXME: only create these if not found
        auto discard = session.getContext().newOpaqueJSON("sysrepo", "discard-items", libyang::JSON{xpath});

        if (!parent) {
            parent = discard;
        } else {
            parent->insertSibling(*discard);
        }
    }

    for (const auto& [propertyName, value] : values) {
        if (!parent) {
            parent = session.getContext().newPath(propertyName, value, libyang::CreationOptions::Output);
        } else {
            parent->newPath(propertyName, value, libyang::CreationOptions::Update | libyang::CreationOptions::Output);
        }
    }

    for (const auto& xpath : ourRemovals) {
        if (!parent) {
            log->trace("Cannot remove {} from stored ops edit: no data", xpath);
        } else if (auto node = parent->findPath(xpath)) {
            node->unlink();
        } else {
            log->trace("Cannot remove {} from stored ops edit: not found", xpath);
        }
    }
}

/** @brief Update the operational DS */
void valuesPush(sysrepo::Session session, const YANGData& values, const std::vector<std::string>& foreignRemovals, const std::vector<std::string>& ourRemovals)
{
    WITH_TIME_MEASUREMENT{};
    if (values.empty() && foreignRemovals.empty() && ourRemovals.empty())
        return;

    ScopedDatastoreSwitch s(session, sysrepo::Datastore::Operational);
    auto edit = session.operationalChanges();
    valuesToYang(values, foreignRemovals, ourRemovals, session, edit);

    if (edit) {
        session.editBatch(*edit, sysrepo::DefaultOperation::Replace);
        spdlog::get("main")->trace("valuesPush: {}", *session.getPendingChanges()->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings));
        WITH_TIME_MEASUREMENT("valuesPush/applyChanges");
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
