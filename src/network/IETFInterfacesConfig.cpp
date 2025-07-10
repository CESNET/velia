/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <numeric>
#include "IETFInterfacesConfig.h"
#include "utils/io.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;

namespace {

const auto CZECHLIGHT_NETWORK_MODULE_NAME = "czechlight-network"s;
const auto IETF_IP_MODULE_NAME = "ietf-ip"s;
const auto IETF_INTERFACES_MODULE_NAME = "ietf-interfaces"s;
const auto IETF_ROUTING_MODULE_NAME = "ietf-routing"s;
const auto IETF_IPV4_UNICAST_ROUTING_MODULE_NAME = "ietf-ipv4-unicast-routing";
const auto IETF_IPV6_UNICAST_ROUTING_MODULE_NAME = "ietf-ipv6-unicast-routing";
const auto IETF_INTERFACES = "/"s + IETF_INTERFACES_MODULE_NAME + ":interfaces"s;

using NetworkConfiguration = std::map<std::string, std::vector<std::string>>;

std::string generateNetworkConfigFile(const std::string& linkName, const NetworkConfiguration& values)
{
    std::ostringstream oss;

    oss << "[Match]" << std::endl;
    oss << "Name=" << linkName << std::endl;

    for (const auto& [sectionName, values] : values) {
        oss << std::endl
            << '[' << sectionName << ']' << std::endl;

        for (const auto& confValue : values) {
            oss << confValue << std::endl;
        }
    }

    return oss.str();
}

/** @brief Checks if protocol is enabled.
 *
 * If the ietf-ip:ipv{4,6} presence container is present, takes value of leaf 'enabled' (which is always there). If the container is not present (and so the 'enabled' leaf is not there as well), then the protocol is disabled.
 */
bool protocolEnabled(const libyang::DataNode& linkEntry, const std::string& proto)
{
    const auto xpath = "ietf-ip:" + proto + "/enabled";

    if (auto node = velia::utils::getUniqueSubtree(linkEntry, xpath)) {
        return velia::utils::asString(node.value()) == "true"s;
    }

    return false;
}

/** @brief Adds values to [Network] section of systemd.network(5) config file. */
void addNetworkConfig(NetworkConfiguration& configValues, const std::string& linkName, const libyang::DataNode& linkEntry) {
    if (auto node = velia::utils::getUniqueSubtree(linkEntry, "description")) {
        configValues["Network"].push_back("Description="s + velia::utils::asString(node.value()));
    }

    // if addresses present, generate them...
    for (const auto& ipProto : {"ipv4", "ipv6"}) {
        // ...but only if the protocol is enabled
        if (!protocolEnabled(linkEntry, ipProto)) {
            continue;
        }

        const auto IPAddressListXPath = "ietf-ip:"s + ipProto + "/ietf-ip:address";
        const auto addresses = linkEntry.findXPath(IPAddressListXPath);

        for (const auto& ipEntry : addresses) {
            auto ipAddress = velia::utils::asString(velia::utils::getUniqueSubtree(ipEntry, "ip").value());
            auto prefixLen = velia::utils::asString(velia::utils::getUniqueSubtree(ipEntry, "prefix-length").value());

            spdlog::get("system")->trace("Link {}: address {}/{} configured", linkName, ipAddress, prefixLen);
            configValues["Network"].push_back("Address="s + ipAddress + "/" + prefixLen);
        }
    }

    // systemd-networkd auto-generates IPv6 link-layer addresses https://www.freedesktop.org/software/systemd/man/systemd.network.html#LinkLocalAddressing=
    // disable this behaviour when IPv6 is disabled or when link enslaved
    bool isSlave = false;

    if (auto node = velia::utils::getUniqueSubtree(linkEntry, "czechlight-network:bridge")) {
        configValues["Network"].push_back("Bridge="s + velia::utils::asString(node.value()));
        isSlave = true;
    }

    if (!protocolEnabled(linkEntry, "ipv6") && !isSlave) {
        configValues["Network"].push_back("LinkLocalAddressing=no");
    }

    // network autoconfiguration
    if (auto node = velia::utils::getUniqueSubtree(linkEntry, "ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses"); protocolEnabled(linkEntry, "ipv6") && velia::utils::asString(node.value()) == "true"s) {
        configValues["Network"].push_back("IPv6AcceptRA=true");
    } else {
        configValues["Network"].push_back("IPv6AcceptRA=false");
    }

    if (auto node = velia::utils::getUniqueSubtree(linkEntry, "ietf-ip:ipv4/czechlight-network:dhcp-client"); protocolEnabled(linkEntry, "ipv4") && velia::utils::asString(node.value()) == "true"s) {
        configValues["Network"].push_back("DHCP=ipv4");
    } else {
        configValues["Network"].push_back("DHCP=no");
    }

    configValues["Network"].push_back("LLDP=true");
    configValues["Network"].push_back("EmitLLDP=nearest-bridge");
}
}

namespace velia::network {

IETFInterfacesConfig::IETFInterfacesConfig(::sysrepo::Session srSess, std::filesystem::path configDirectory, std::vector<std::string> managedLinks, reload_cb_t reloadCallback)
    : m_log(spdlog::get("network"))
    , m_reloadCb(std::move(reloadCallback))
    , m_configDirectory(std::move(configDirectory))
    , m_managedLinks(std::move(managedLinks))
    , m_srSession(srSess)
    , m_srSubscribe()
{
    utils::ensureModuleImplemented(m_srSession, IETF_INTERFACES_MODULE_NAME, "2018-02-20");
    utils::ensureModuleImplemented(m_srSession, IETF_IP_MODULE_NAME, "2018-02-22");
    utils::ensureModuleImplemented(m_srSession, IETF_ROUTING_MODULE_NAME, "2018-03-13");
    utils::ensureModuleImplemented(m_srSession, IETF_IPV4_UNICAST_ROUTING_MODULE_NAME, "2018-03-13");
    utils::ensureModuleImplemented(m_srSession, IETF_IPV6_UNICAST_ROUTING_MODULE_NAME, "2018-03-13");
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_NETWORK_MODULE_NAME, "2025-06-06");

    m_srSubscribe = m_srSession.onModuleChange(
        IETF_INTERFACES_MODULE_NAME,
        [this](auto session, auto, auto, auto, auto, auto) { return moduleChange(session); },
        IETF_INTERFACES,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Enabled);
}

sysrepo::ErrorCode IETFInterfacesConfig::moduleChange(::sysrepo::Session session) const
{
    std::map<std::string, std::optional<std::string>> networkConfigFiles;

    for (const auto& linkName : m_managedLinks) {
        auto data = session.getData("/ietf-interfaces:interfaces/interface[name='" + linkName + "']");
        if (!data) {
            m_log->debug("Link {} not configured", linkName);
            networkConfigFiles[linkName] = std::nullopt;
            continue;
        }

        auto linkEntry = *data->findPath("/ietf-interfaces:interfaces/interface[name='" + linkName + "']");

        if (auto node = utils::getUniqueSubtree(linkEntry, "enabled"); !node || utils::asString(node.value()) != "true"s) {
            m_log->debug("Link {} disabled", linkName);
            networkConfigFiles[linkName] = std::nullopt;
            continue;
        }

        NetworkConfiguration configValues;
        addNetworkConfig(configValues, linkName, linkEntry);

        networkConfigFiles[linkName] = generateNetworkConfigFile(linkName, configValues);
    }

    auto changedLinks = updateNetworkFiles(networkConfigFiles, m_configDirectory);
    m_reloadCb(changedLinks);
    return sysrepo::ErrorCode::Ok;
}

std::string disabledConfiguration(const std::string& linkName)
{
    return R"([Match]
Name=)" + linkName + R"(
[Network]
DHCP=no
LinkLocalAddressing=no
IPv6AcceptRA=no
)";
}

IETFInterfacesConfig::ChangedUnits IETFInterfacesConfig::updateNetworkFiles(const std::map<std::string, std::optional<std::string>>& networkConfig, const std::filesystem::path& configDir) const
{
    ChangedUnits ret;

    for (const auto& link : m_managedLinks) {
        const auto targetFile = configDir / ("10-"s + link + ".network");
        const bool fileExists = std::filesystem::exists(targetFile);
        const auto& configuration = networkConfig.at(link);

        // If the file exists and the content is the same as configuration, no need to change anything
        if (fileExists && velia::utils::readFileToString(targetFile) == networkConfig.at(link).value_or(disabledConfiguration(link))) {
            continue;
        }

        if (configuration) {
            velia::utils::safeWriteFile(targetFile, *configuration);
            ret.changedOrNew.push_back(link);
        } else {
            // configuration removed: write empty file which replaces the default configuration, effectively disabling the link
            velia::utils::safeWriteFile(targetFile, disabledConfiguration(link));
            ret.deleted.push_back(link);
        }
    }

    return ret;
}

}
