/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <fmt/format.h>
#include <numeric>
#include "IETFInterfacesConfig.h"
#include "utils/io.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;
using namespace fmt::literals;

namespace {

const auto CZECHLIGHT_NETWORK_MODULE_NAME = "czechlight-network"s;
const auto IETF_IP_MODULE_NAME = "ietf-ip"s;
const auto IETF_INTERFACES_MODULE_NAME = "ietf-interfaces"s;
const auto IETF_ROUTING_MODULE_NAME = "ietf-routing"s;
const auto IETF_IPV4_UNICAST_ROUTING_MODULE_NAME = "ietf-ipv4-unicast-routing";
const auto IETF_IPV6_UNICAST_ROUTING_MODULE_NAME = "ietf-ipv6-unicast-routing";
const auto IETF_INTERFACES = "/"s + IETF_INTERFACES_MODULE_NAME + ":interfaces"s;

const std::string NETWORK_FILE_CONTENT_TEMPLATE = R"([Match]
Name={linkName}

[Network]
LLDP=true
EmitLLDP=nearest-bridge
{dhcp}
{address})";

}

namespace velia::system {

IETFInterfacesConfig::IETFInterfacesConfig(std::shared_ptr<::sysrepo::Session> srSess, std::filesystem::path configDirectory, std::vector<std::string> managedLinks, reload_cb_t reloadCallback)
    : m_log(spdlog::get("system"))
    , m_reloadCb(std::move(reloadCallback))
    , m_configDirectory(std::move(configDirectory))
    , m_managedLinks(std::move(managedLinks))
    , m_srSession(std::move(srSess))
    , m_srSubscribe(std::make_shared<::sysrepo::Subscribe>(m_srSession))
{
    utils::ensureModuleImplemented(m_srSession, IETF_INTERFACES_MODULE_NAME, "2018-02-20");
    utils::ensureModuleImplemented(m_srSession, IETF_IP_MODULE_NAME, "2018-02-22");
    utils::ensureModuleImplemented(m_srSession, IETF_ROUTING_MODULE_NAME, "2018-03-13");
    utils::ensureModuleImplemented(m_srSession, IETF_IPV4_UNICAST_ROUTING_MODULE_NAME, "2018-03-13");
    utils::ensureModuleImplemented(m_srSession, IETF_IPV6_UNICAST_ROUTING_MODULE_NAME, "2018-03-13");
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_NETWORK_MODULE_NAME, "2021-02-22");

    m_srSubscribe->module_change_subscribe(
        IETF_INTERFACES_MODULE_NAME.c_str(), [this](auto session, auto, auto, auto, auto) { return onUpdate(session); }, "/ietf-interfaces:interfaces", 0, SR_SUBSCR_DONE_ONLY);
}

int IETFInterfacesConfig::onUpdate(std::shared_ptr<::sysrepo::Session> session) const
{
    spdlog::get("system")->trace("ietf-interfaces module change callback");
    std::map<std::string, std::string> networkConfig;


    if (auto data = session->get_data("/ietf-interfaces:interfaces/interface"); data) {
        auto linkEntries = data->find_path("/ietf-interfaces:interfaces/interface");
        for (const auto& linkEntry : linkEntries->data()) {
            auto linkName = getValueAsString(getSubtree(linkEntry, "name"));

            std::vector<std::pair<std::string, std::string>> IPsWithPrefixLen;
            for (const auto& ipProto : {"ipv4", "ipv6"}) {
                const auto IPAddressListXPath = "ietf-ip:"s + ipProto + "/ietf-ip:address";
                const auto addresses = linkEntry->find_path(IPAddressListXPath.c_str());

                for (const auto& ipEntry : addresses->data()) {
                    auto ip = getValueAsString(getSubtree(ipEntry, "ip"));
                    auto prefixLen = getValueAsString(getSubtree(ipEntry, "prefix-length"));

                    spdlog::get("system")->trace("Link {}: address {}/{} added", linkName, ip, prefixLen);
                    IPsWithPrefixLen.emplace_back(ip, prefixLen);
                }
            }

            auto addressSetting = std::accumulate(IPsWithPrefixLen.begin(), IPsWithPrefixLen.end(), std::string(), [](const std::string& acc, const std::pair<std::string, std::string>& kv) {
                return acc + "Address=" + kv.first + "/" + kv.second + "\n"; // see man systemd.network(5)
            });

            std::string dhcpSetting;
            {
                bool DHCPv4 = linkEntry->find_path("ietf-ip:ipv4/czechlight-network:dhcp")->size() > 0;
                bool DHCPv6 = linkEntry->find_path("ietf-ip:ipv6/czechlight-network:dhcp")->size() > 0;

                spdlog::get("system")->trace("Link {}: DHCP IPv4={} IPv6={}", linkName, DHCPv4, DHCPv6);

                if (DHCPv4 && DHCPv6) {
                    dhcpSetting = "DHCP=yes";
                } else if (DHCPv4) {
                    dhcpSetting = "DHCP=ipv4";
                } else if (DHCPv6) {
                    dhcpSetting = "DHCP=ipv6";
                } else {
                    dhcpSetting = "DHCP=no";
                }
            }

            networkConfig[linkName] = fmt::format(NETWORK_FILE_CONTENT_TEMPLATE,
                                                  "linkName"_a = linkName,
                                                  "address"_a = addressSetting,
                                                  "dhcp"_a = dhcpSetting);
        }
    }

    auto changedLinks = writeConfigs(networkConfig, m_configDirectory);
    m_reloadCb(changedLinks);
    return SR_ERR_OK;
}

std::vector<std::string> IETFInterfacesConfig::writeConfigs(const std::map<std::string, std::string>& networkConfig, const std::filesystem::path& configDir) const
{
    std::vector<std::string> changedLinks;

    for (const auto& link : m_managedLinks) {
        const auto targetFile = configDir / (link + ".network");
        const bool fileExists = std::filesystem::exists(targetFile);
        const bool updateExists = networkConfig.contains(link);

        // nothing changed, file doesn't exist -> keep default configuration
        if (!fileExists && !updateExists) {
            continue;
        }

        // updated, file exists, but both with same content -> keep current configuration
        if (fileExists && updateExists && velia::utils::readFileToString(targetFile) == networkConfig.at(link)) {
            continue;
        }

        if (updateExists) {
            velia::utils::safeWriteFile(targetFile, networkConfig.at(link));
        } else { // configuration removed
            std::filesystem::remove(targetFile);
        }

        changedLinks.push_back(link);
    }

    return changedLinks;
}

}
