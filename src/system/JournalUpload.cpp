/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <filesystem>
#include <fstream>
#include <regex>
#include "JournalUpload.h"
#include "utils/io.h"
#include "utils/libyang.h"
#include "utils/log.h"

using namespace std::string_literals;

static const auto UPLOAD_URL_CONTAINER = "/czechlight-system:journal-upload";

namespace {

std::optional<std::string> extractURL(sysrepo::Session session)
{
    auto data = session.getData(UPLOAD_URL_CONTAINER);
    if (!data) {
        return std::nullopt;
    }

    std::string url;

    auto hostNode = data->findPath(UPLOAD_URL_CONTAINER + "/host"s);
    auto hostValue = velia::utils::asString(*hostNode);
    auto isIpv6 = hostNode->asTerm().valueType().internalPluginId().find("ipv6") != std::string::npos;

    url += data->findPath(UPLOAD_URL_CONTAINER + "/protocol"s)->asTerm().valueStr();
    url += "://";

    if (isIpv6) {
        url += "[" + hostValue + "]";
    } else {
        url += hostValue;
    }

    url += ":";
    url += data->findPath(UPLOAD_URL_CONTAINER + "/port"s)->asTerm().valueStr();

    return url;
}

sysrepo::ErrorCode configureJournalUpload(velia::Log log, const std::optional<std::string>& url, const std::filesystem::path& envFile, const velia::system::JournalUpload::RestartCb& restartCb)
{
    std::optional<std::string> oldContent;
    std::optional<std::string> newContent;

    try {
        oldContent = velia::utils::readFileToString(envFile);
    } catch (const std::invalid_argument&) {
        // if the file does not exist, oldContent will still be nullopt
    }

    if (url) {
        newContent = "DESTINATION=" + *url + '\n';
    }

    if (oldContent != newContent) {
        if (newContent) {
            std::ofstream ofs(envFile);
            ofs << *newContent;

            newContent->pop_back(); // remove the \n
            log->trace("systemd-journal-upload.service environment file {} set to {}", std::string(envFile), *newContent);
        } else {
            std::filesystem::remove(envFile);
            log->trace("systemd-journal-upload.service environment file {} removed", std::string(envFile));
        }

        restartCb(log);
    }

    return sysrepo::ErrorCode::Ok;
}

sysrepo::Subscription journalUploadSubscription(velia::Log log, sysrepo::Session session, const std::filesystem::path& envFile, const velia::system::JournalUpload::RestartCb& restartCb)
{
    auto sub = session.onModuleChange(
        "czechlight-system",
        [log, envFile, restartCb](auto session, auto, auto, auto, auto, auto) {
            return configureJournalUpload(log, extractURL(session), envFile, restartCb);
        },
        std::nullopt,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Enabled);

    /*
     * In case someone removes the presence container between sysrepo loads the data and this module startup we won't get a change (Deleted) from sysrepo and the file won't be written.
     * Therefore, first register the callback and than call the configure manually.
     * The configure function does not restart the service unless the configuration file content changes so this should not trigger the unit restart.
     */
    configureJournalUpload(log, extractURL(session), envFile, restartCb);

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
