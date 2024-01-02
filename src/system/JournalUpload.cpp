/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <filesystem>
#include <regex>
#include <sysrepo-cpp/Enum.hpp>
#include "JournalUpload.h"
#include "utils/io.h"
#include "utils/log.h"

using namespace std::string_literals;

static const auto UPLOAD_URL_CONTAINER = "/czechlight-system:journal-upload";

namespace {

/** @brief Checks if provided string is a string representing IPv6 address */
bool isIPv6(const std::string& address) {
    // regex patterns copied from ietf-inet-types@2013-07-15.yang: typedef ipv6-address
    static const std::string patternA(R"(((:|[0-9a-fA-F]{0,4}):)([0-9a-fA-F]{0,4}:){0,5}((([0-9a-fA-F]{0,4}:)?(:|[0-9a-fA-F]{0,4}))|(((25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9]?[0-9])))(%[\p{N}\p{L}]+)?)");
    static const std::string patternB(R"((([^:]+:){6}(([^:]+:[^:]+)|(.*\..*)))|((([^:]+:)*[^:]+)?::(([^:]+:)*[^:]+)?)(%.+)?)");

    static const std::regex ipv6("((" + patternA + ")|(" + patternB + "))");
    std::smatch m;
    return std::regex_match(address, m, ipv6);
}

std::optional<std::string> extractURL(sysrepo::Session session)
{
    if (auto data = session.getData(UPLOAD_URL_CONTAINER)) {
        std::string url;

        auto host = std::string{data->findPath(UPLOAD_URL_CONTAINER + "/host"s)->asTerm().valueStr()};

        url += data->findPath(UPLOAD_URL_CONTAINER + "/protocol"s)->asTerm().valueStr();
        url += "://";

        // IPv6 adresses are wrapped inside [].
        // Is it possible to detect in libyang which member of the union type inet:host did this string conform to?
        // I did not find an easy way, so let's validate against our regexp.
        if (isIPv6(host)) {
            url += "[" + host + "]";
        } else {
            url += host;
        }

        url += ":";
        url += std::string(data->findPath(UPLOAD_URL_CONTAINER + "/port"s)->asTerm().valueStr());

        return url;
    }

    return std::nullopt;
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
            log->debug("systemd-journal-upload.service environment file {} set to {}", std::string(envFile), *newContent);
        } else {
            std::filesystem::remove(envFile);
            log->debug("systemd-journal-upload.service environment file {} removed", std::string(envFile));
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
