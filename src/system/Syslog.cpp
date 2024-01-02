/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <filesystem>
#include "Syslog.h"
#include "utils/io.h"
#include "utils/log.h"

using namespace std::string_literals;

static const auto DBUS_SYSTEMD_INTERFACE = "org.freedesktop.systemd1.Manager";
static const auto DBUS_SYSTEMD_MANAGER = "/org/freedesktop/systemd1";
static const auto DBUS_SYSTEMD_BUS = "org.freedesktop.systemd1";
static const auto SYSTEMD_JOURNAL_UPLOAD_SERVICE = "systemd-journal-upload.service"s;

namespace {
void configureJournalUpload(velia::Log log, const std::filesystem::path& envFile, const std::optional<std::string>& url, sdbus::IProxy& sdManager)
{
    std::optional<std::string> oldContent;
    std::optional<std::string> newContent;

    try {
        oldContent = velia::utils::readFileToString(envFile);
    } catch (const std::invalid_argument&) {
    }

    if (url) {
        newContent = "DESTINATION=" + *url + "\n";
    }

    if (oldContent != newContent) {
        if (newContent) {
            std::ofstream ofs(envFile);
            ofs << *newContent;

            newContent->pop_back(); // remove the \n
            log->debug("systemd-journal-upload.service environment file set to {}", *newContent);
        } else {
            std::filesystem::remove(envFile);
            log->debug("systemd-journal-upload.service environment file removed");
        }

        log->debug("Restarting systemd-journal-upload.service");
        sdManager.callMethod("RestartUnit").onInterface(DBUS_SYSTEMD_INTERFACE).withArguments(SYSTEMD_JOURNAL_UPLOAD_SERVICE, "replace"s);
    }
}

}

namespace velia::system {

Syslog::Syslog(sysrepo::Connection conn, sdbus::IConnection& dbusConnection, const std::filesystem::path& journalUploadEnvFile)
    : Syslog(conn, dbusConnection, DBUS_SYSTEMD_BUS, journalUploadEnvFile)
{
}

Syslog::Syslog(sysrepo::Connection conn, sdbus::IConnection& dbusConnection, const std::string& dbusBusName, const std::filesystem::path& journalUploadEnvFile)
    : m_sdManager(sdbus::createProxy(dbusConnection, dbusBusName, DBUS_SYSTEMD_MANAGER))
    , m_log(spdlog::get("system"))
{
    auto sess = conn.sessionStart();

    /* reset journal-upload settings:
     * In case someone removes the presence container between sysrepo loads the data and this module startup we won't get a change (Deleted) from sysrepo and the file won't be written.
     * Therefore, first check the current state and configure journal-upload.
     * Then, start the module change CB. In case someone changes the data between first and second configure call, it is no problem. In case nothing gets changed, no restart will be done, because
     * the configure restarts the unit only when the configuration changes.
     */

    std::optional<std::string> url;
    if (auto data = sess.getData("/czechlight-system:syslog")) {
        if (auto node = data->findPath("/czechlight-system:syslog/journal-upload/url")) {
            url = node->asTerm().valueStr();
        }
    }
    configureJournalUpload(m_log, journalUploadEnvFile, url, *m_sdManager);

    m_srSub = sess.onModuleChange(
        "czechlight-system",
        [&, this](sysrepo::Session session, auto, auto, auto, auto, auto) {
            for (const auto& change : session.getChanges()) {
                if (change.node.path() == "/czechlight-system:syslog/journal-upload/url") {
                    std::optional<std::string> url;
                    if (change.operation != sysrepo::ChangeOperation::Deleted) {
                        url = change.node.asTerm().valueStr();
                    }
                    configureJournalUpload(m_log, journalUploadEnvFile, url, *m_sdManager);
                    break;
                }
            }
            return sysrepo::ErrorCode::Ok;
        },
        std::nullopt,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Enabled /* to pick up changes between initial upload and now */);
}

}
