/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <filesystem>
#include <sysrepo-cpp/Enum.hpp>
#include "JournalUpload.h"
#include "utils/io.h"
#include "utils/log.h"

using namespace std::string_literals;

static const auto UPLOAD_URL_LEAF = "/czechlight-system:syslog/journal-upload/url";

namespace {
void configureJournalUpload(velia::Log log, const std::filesystem::path& envFile, const std::optional<std::string>& url, const velia::system::JournalUpload::RestartCb& restartCb)
{
    std::optional<std::string> oldContent;
    std::optional<std::string> newContent;

    try {
        oldContent = velia::utils::readFileToString(envFile);
    } catch (const std::invalid_argument&) {
        // if the file does not exist, oldContent will still be nullopt
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

        restartCb(log);
    }
}

sysrepo::Subscription journalUploadSubscription(velia::Log log, sysrepo::Session session, const std::filesystem::path& envFile, const velia::system::JournalUpload::RestartCb& restartCb)
{
    auto sub = session.onModuleChange(
        "czechlight-system",
        [log, envFile, restartCb](auto session, auto, auto, auto, auto, auto) {
            for (const auto& change : session.getChanges()) {
                if (change.node.path() == UPLOAD_URL_LEAF) {
                    std::optional<std::string> url;
                    if (change.operation != sysrepo::ChangeOperation::Deleted) { // value modified
                        url = change.node.asTerm().valueStr();
                    }
                    configureJournalUpload(log, envFile, url, restartCb);
                    break;
                }
            }
            return sysrepo::ErrorCode::Ok;
        },
        std::nullopt,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Enabled);

    /*
     * In case someone removes the presence container between sysrepo loads the data and this module startup we won't get a change (Deleted) from sysrepo and the file won't be written.
     * Therefore, first register the callback and than call the configure manually.
     * The configure function does not restart the service unless the configuration file content changes so this should not trigger the unit restart.
     */
    std::optional<std::string> url;
    if (auto data = session.getData("/czechlight-system:syslog")) {
        if (auto node = data->findPath(UPLOAD_URL_LEAF)) {
            url = node->asTerm().valueStr();
        }
    }

    configureJournalUpload(log, envFile, url, restartCb);

    return sub;
}

}

namespace velia::system {

JournalUpload::JournalUpload(sysrepo::Session session, const std::filesystem::path& envFile, const RestartCb& restartCb)
    : m_log(spdlog::get("system"))
    , m_srSub(journalUploadSubscription(m_log, session, envFile, restartCb))
{
}

}
