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

std::string generateNetworkConfigFile(const std::string& linkName, const std::map<std::string, std::vector<std::string>>& values)
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
}

namespace velia::system {

IETFInterfacesConfig::IETFInterfacesConfig(::sysrepo::Session srSess, std::filesystem::path configDirectory, std::vector<std::string> managedLinks, reload_cb_t reloadCallback)
    : m_log(spdlog::get("system"))
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
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_NETWORK_MODULE_NAME, "2021-02-22");

    m_srSubscribe = m_srSession.onModuleChange(
        IETF_INTERFACES_MODULE_NAME,
        [this](auto session, auto, auto, auto, auto, auto) { return moduleChange(session); },
        IETF_INTERFACES,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Enabled);
}

sysrepo::ErrorCode IETFInterfacesConfig::moduleChange(::sysrepo::Session session) const
{
    std::map<std::string, std::string> networkConfigFiles;

    if (auto data = session.getData("/ietf-interfaces:interfaces/interface")) {
        auto linkEntries = data->findXPath("/ietf-interfaces:interfaces/interface");
        for (const auto& linkEntry : linkEntries) {
            std::map<std::string, std::vector<std::string>> configValues;

            auto linkName = utils::asString(utils::getUniqueSubtree(linkEntry, "name").value());

            if (auto node = utils::getUniqueSubtree(linkEntry, "description")) {
                configValues["Network"].push_back("Description="s + utils::asString(node.value()));
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
                    auto ipAddress = utils::asString(utils::getUniqueSubtree(ipEntry, "ip").value());
                    auto prefixLen = utils::asString(utils::getUniqueSubtree(ipEntry, "prefix-length").value());

                    spdlog::get("system")->trace("Link {}: address {}/{} configured", linkName, ipAddress, prefixLen);
                    configValues["Network"].push_back("Address="s + ipAddress + "/" + prefixLen);
                }
            }

            // systemd-networkd auto-generates IPv6 link-layer addresses https://www.freedesktop.org/software/systemd/man/systemd.network.html#LinkLocalAddressing=
            // disable this behaviour when IPv6 is disabled or when link enslaved
            bool isSlave = false;

            if (auto node = utils::getUniqueSubtree(linkEntry, "czechlight-network:bridge")) {
                configValues["Network"].push_back("Bridge="s + utils::asString(node.value()));
                isSlave = true;
            }

            if (!protocolEnabled(linkEntry, "ipv6") && !isSlave) {
                configValues["Network"].push_back("LinkLocalAddressing=no");
            }

            // network autoconfiguration
            if (auto node = utils::getUniqueSubtree(linkEntry, "ietf-ip:ipv6/ietf-ip:autoconf/ietf-ip:create-global-addresses"); protocolEnabled(linkEntry, "ipv6") && utils::asString(node.value()) == "true"s) {
                configValues["Network"].push_back("IPv6AcceptRA=true");
            } else {
                configValues["Network"].push_back("IPv6AcceptRA=false");
            }

            if (auto node = utils::getUniqueSubtree(linkEntry, "ietf-ip:ipv4/czechlight-network:dhcp-client"); protocolEnabled(linkEntry, "ipv4") && utils::asString(node.value()) == "true"s) {
                configValues["Network"].push_back("DHCP=ipv4");
            } else {
                configValues["Network"].push_back("DHCP=no");
            }

            configValues["Network"].push_back("LLDP=true");
            configValues["Network"].push_back("EmitLLDP=nearest-bridge");

            networkConfigFiles[linkName] = generateNetworkConfigFile(linkName, configValues);
        }
    }

    auto changedLinks = updateNetworkFiles(networkConfigFiles, m_configDirectory);
    m_reloadCb(changedLinks);
    return sysrepo::ErrorCode::Ok;
}

IETFInterfacesConfig::ChangedUnits IETFInterfacesConfig::updateNetworkFiles(const std::map<std::string, std::string>& networkConfig, const std::filesystem::path& configDir) const
{
    ChangedUnits ret;

    for (const auto& link : m_managedLinks) {
        const auto targetFile = configDir / (link + ".network");
        const bool fileExists = std::filesystem::exists(targetFile);
        const bool updateExists = networkConfig.contains(link);

        // no configuration for link present and the file doesn't even exist -> keep default configuration
        if (!fileExists && !updateExists) {
            continue;
        }

        // configuration for link present, file exists, but it has the same content as the new one -> keep current configuration, no need to rewrite
        if (fileExists && updateExists && velia::utils::readFileToString(targetFile) == networkConfig.at(link)) {
            continue;
        }

        if (updateExists) {
            velia::utils::safeWriteFile(targetFile, networkConfig.at(link));
            ret.changedOrNew.push_back(link);
        } else { // configuration removed
            std::filesystem::remove(targetFile);
            ret.deleted.push_back(link);
        }
    }

    return ret;
}

}
