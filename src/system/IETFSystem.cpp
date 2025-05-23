/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <arpa/inet.h>
#include <fstream>
#include <libyang-cpp/Time.hpp>
#include <optional>
#include "IETFSystem.h"
#include "system_vars.h"
#include "utils/exec.h"
#include "utils/io.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;

namespace {

const auto IETF_SYSTEM_MODULE_NAME = "ietf-system"s;
const auto IETF_SYSTEM_STATE_MODULE_PREFIX = "/"s + IETF_SYSTEM_MODULE_NAME + ":system-state/"s;
const auto IETF_SYSTEM_HOSTNAME_PATH = "/ietf-system:system/hostname";
const auto IETF_SYSTEM_DNS_PATH = "/ietf-system:system/dns-resolver";
const auto IETF_SYSTEM_STATE_CLOCK_PATH = "/ietf-system:system-state/clock";

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
        if (line.empty() || line.starts_with("#")) {
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

std::optional<std::string> getHostnameFromChange(const sysrepo::Session session)
{
    std::optional<std::string> res;

    auto data = session.getData(IETF_SYSTEM_HOSTNAME_PATH);
    if (data) {
        auto hostnameNode = data->findPath(IETF_SYSTEM_HOSTNAME_PATH);
        res = hostnameNode->asTerm().valueStr();
    }

    return res;
}

/** @brief Returns list of IP addresses (coded as a string) that serve as the DNS servers.
 *
 * We query the addresses from systemd-resolved D-Bus interface (see https://www.freedesktop.org/software/systemd/man/org.freedesktop.resolve1.html#Properties
 * and possibly also https://www.freedesktop.org/software/systemd/man/resolved.conf.html).
 * We use the value of DnsEx property on the Manager object. In case that DnsEx is empty we fallback to FallbackDnsEx property.
 *
 * Note that the returns not only the system-wide setting, but also the DNS resolvers that are configured per-interface. We chose not to ignore them despite ietf-system
 * YANG model inability to distinguish between system-wide and per-interface type. Hence the resolver is listed as a system-wide one.
 */
std::vector<std::string> getDNSResolvers(sdbus::IConnection& connection, const std::string& dbusName)
{
    static const auto DBUS_RESOLVE1_MANAGER_PATH = "/org/freedesktop/resolve1";
    static const auto DBUS_RESOLVE1_MANAGER_INTERFACE = "org.freedesktop.resolve1.Manager";

    auto proxy = sdbus::createProxy(connection, dbusName, DBUS_RESOLVE1_MANAGER_PATH);

    for (const auto& propertyName : {"DNSEx", "FallbackDNSEx"}) {
        sdbus::Variant store = proxy->getProperty(propertyName).onInterface(DBUS_RESOLVE1_MANAGER_INTERFACE);

        // DBus type of the DNSEx and FallbackDNSEx properties is "a(iiayqs)" ~ Array of [ Struct of (Int32, Int32, Array of [Byte], Uint16, String) ]
        // i.e., <ifindex (0 for system-wide), addrtype, address as a bytearray, port (0 for unspecified), server name>,
        auto replyObjects = store.get<std::vector<sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>, uint16_t, std::string>>>();

        if (!replyObjects.empty() > 0) {
            std::vector<std::string> res;

            for (const auto& e : replyObjects) {
                auto addrType = e.get<1>();
                auto addrBytes = e.get<2>();

                std::array<char, std::max(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)> buf{};
                inet_ntop(addrType, addrBytes.data(), buf.data(), buf.size());

                res.emplace_back(buf.data());
            }

            return res;
        }
    }

    return {};
}
}

namespace velia::system {

void IETFSystem::initStaticProperties(const std::filesystem::path& osRelease, const std::filesystem::path& machineIdPath)
{
    utils::ensureModuleImplemented(m_srSession, IETF_SYSTEM_MODULE_NAME, "2014-08-06");

    utils::YANGData opsSystemStateData;
    std::map<std::string, std::string> osReleaseContents = parseKeyValueFile(osRelease);
    auto machineId = utils::readFileString(machineIdPath);

    for (const auto& [key, leaf] : {
             std::pair<std::string, std::string>{"NAME", "platform/os-name"},
             {"VERSION", "platform/os-release"},
             {"VERSION", "platform/os-version"}}) {
        auto it = osReleaseContents.find(key);
        if (it == osReleaseContents.end()) {
            throw std::out_of_range("Could not read key " + key + " from file " + osRelease.string());
        }

        opsSystemStateData.emplace_back(IETF_SYSTEM_STATE_MODULE_PREFIX + leaf, it->second);
    }

    opsSystemStateData.emplace_back(IETF_SYSTEM_STATE_MODULE_PREFIX + "platform/czechlight-system:machine-id", machineId);

    utils::valuesPush(m_srSession, opsSystemStateData, {}, {});
}

void IETFSystem::initSystemRestart()
{
    sysrepo::RpcActionCb cb = [this](auto session, auto, auto, auto, auto, auto, auto) {
            try {
                velia::utils::execAndWait(m_log, SYSTEMCTL_EXECUTABLE, {"reboot"}, "", {});
            } catch(const std::runtime_error& e) {
                utils::setErrors(session, "Reboot procedure failed.");
                return sysrepo::ErrorCode::OperationFailed;
            }

            return sysrepo::ErrorCode::Ok;
        };

    m_srSubscribe = m_srSession.onRPCAction("/" + IETF_SYSTEM_MODULE_NAME + ":system-restart", cb);
}

void IETFSystem::initHostname()
{
    sysrepo::ModuleChangeCb hostNameCbRunning = [this] (auto session, auto, auto, auto, auto, auto) {
        if (auto newHostname = getHostnameFromChange(session)) {
            velia::utils::execAndWait(m_log, HOSTNAMECTL_EXECUTABLE, {"set-hostname", *newHostname}, "");
        }
        return sysrepo::ErrorCode::Ok;
    };

    sysrepo::ModuleChangeCb hostNameCbStartup = [] (auto session, auto, auto, auto, auto, auto) {
        if (auto newHostname = getHostnameFromChange(session)) {
            utils::safeWriteFile(BACKUP_ETC_HOSTNAME_FILE, *newHostname);
        }
        return sysrepo::ErrorCode::Ok;
    };

    sysrepo::OperGetCb hostNameCbOperational = [] (auto, auto, auto, auto, auto, auto, auto& parent) {
        // + 1 for null-terminating byte, HOST_NAME_MAX doesn't count that
        std::array<char, HOST_NAME_MAX + 1> buffer{};

        if (gethostname(buffer.data(), buffer.size()) != 0) {
            throw std::system_error(errno, std::system_category(), "gethostname() failed");
        }

        parent->newPath( IETF_SYSTEM_HOSTNAME_PATH, buffer.data());

        return sysrepo::ErrorCode::Ok;
    };

    m_srSubscribe->onModuleChange(IETF_SYSTEM_MODULE_NAME, hostNameCbRunning, IETF_SYSTEM_HOSTNAME_PATH, 0, sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Enabled);
    m_srSession.switchDatastore(sysrepo::Datastore::Startup);
    m_srSubscribe->onModuleChange(IETF_SYSTEM_MODULE_NAME, hostNameCbStartup, IETF_SYSTEM_HOSTNAME_PATH, 0, sysrepo::SubscribeOptions::DoneOnly);
    m_srSession.switchDatastore(sysrepo::Datastore::Operational);
    m_srSubscribe->onOperGet(IETF_SYSTEM_MODULE_NAME, hostNameCbOperational, IETF_SYSTEM_HOSTNAME_PATH);
}

/** @short Acknowledge writes to dummy fields so that they're visible in the operational DS */
void IETFSystem::initDummies()
{
    m_srSession.switchDatastore(sysrepo::Datastore::Running);
    sysrepo::ModuleChangeCb ignore = [] (auto, auto, auto, auto, auto, auto) {
        return sysrepo::ErrorCode::Ok;
    };
    for (const auto xpath : {"/ietf-system:system/location", "/ietf-system:system/contact"}) {
        m_srSubscribe->onModuleChange(IETF_SYSTEM_MODULE_NAME, ignore, xpath, 0, sysrepo::SubscribeOptions::DoneOnly /* it's a dummy write, no need for SubscribeOptions::Enabled */);
    }
}

/** @short Time and clock callbacks */
void IETFSystem::initClock()
{
    sysrepo::OperGetCb cb = [] (auto, auto, auto, auto, auto, auto, auto& parent) {
        parent->newPath(IETF_SYSTEM_STATE_CLOCK_PATH + "/current-datetime"s, libyang::yangTimeFormat(std::chrono::system_clock::now(), libyang::TimezoneInterpretation::Local));
        return sysrepo::ErrorCode::Ok;
    };

    m_srSubscribe->onOperGet(IETF_SYSTEM_MODULE_NAME, cb, IETF_SYSTEM_STATE_CLOCK_PATH, sysrepo::SubscribeOptions::OperMerge);
}

/** @short DNS resolver callbacks */
void IETFSystem::initDNS(sdbus::IConnection& connection, const std::string& dbusName) {
    sysrepo::OperGetCb dnsOper = [&connection, dbusName] (auto session, auto, auto, auto, auto, auto, auto& parent) {
        utils::YANGData values;
        std::set<std::string> seen;

        /* RFC 7317 specifies that key leaf 'name' contains "An arbitrary name for the DNS server".
           We use the IP address which is unique. If the server is returned multiple times (e.g. once as system-wide and once
           for some specific ifindex, it doesn't matter that it is listed only once. */
        for (const auto& e : getDNSResolvers(connection, dbusName)) {
            if (seen.contains(e)) {
                continue;
            }
            seen.insert(e);
            values.emplace_back(IETF_SYSTEM_DNS_PATH + "/server[name='"s + e + "']/udp-and-tcp/address", e);
        }

        utils::valuesToYang(values, {}, {}, session, parent);
        return sysrepo::ErrorCode::Ok;
    };

    m_srSubscribe->onOperGet(IETF_SYSTEM_MODULE_NAME, dnsOper, IETF_SYSTEM_DNS_PATH);
}

/** This class handles multiple system properties and publishes them via the ietf-system model:
 * - OS-identification data from osRelease file
 * - Rebooting
 * - Hostname
 */
IETFSystem::IETFSystem(::sysrepo::Session srSession, const std::filesystem::path& osRelease, const std::filesystem::path& machineIdPath, sdbus::IConnection& connection, const std::string& dbusName)
    : m_srSession(srSession)
    , m_srSubscribe()
    , m_log(spdlog::get("system"))
{
    initStaticProperties(osRelease, machineIdPath);
    initSystemRestart();
    initHostname();
    initDummies();
    initClock();
    initDNS(connection, dbusName);
}
}
