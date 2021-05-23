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
        IETF_INTERFACES_MODULE_NAME.c_str(), [this](auto session, auto, auto, auto, auto) { return moduleChange(session); }, IETF_INTERFACES.c_str(), 0, SR_SUBSCR_DONE_ONLY);
}

int IETFInterfacesConfig::moduleChange(std::shared_ptr<::sysrepo::Session> session) const
{
    std::map<std::string, std::string> networkConfigFiles;

    if (auto data = session->get_data("/ietf-interfaces:interfaces/interface"); data) {
        auto linkEntries = data->find_path("/ietf-interfaces:interfaces/interface");
        for (const auto& linkEntry : linkEntries->data()) {
            std::map<std::string, std::vector<std::string>> configValues;

            auto linkName = getValueAsString(getSubtree(linkEntry, "name"));

            if (auto set = linkEntry->find_path("description"); set->number() != 0) {
                configValues["Network"].push_back("Description="s + getValueAsString(set->data().front()));
            }

            configValues["Network"].push_back("Bridge=br0");
            configValues["Network"].push_back("LLDP=true");
            configValues["Network"].push_back("EmitLLDP=nearest-bridge");

            networkConfigFiles[linkName] = generateNetworkConfigFile(linkName, configValues);
        }
    }

    auto changedLinks = updateNetworkFiles(networkConfigFiles, m_configDirectory);
    m_reloadCb(changedLinks);
    return SR_ERR_OK;
}

std::vector<std::string> IETFInterfacesConfig::updateNetworkFiles(const std::map<std::string, std::string>& networkConfig, const std::filesystem::path& configDir) const
{
    std::vector<std::string> changedLinks;

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
        } else { // configuration removed
            std::filesystem::remove(targetFile);
        }

        changedLinks.push_back(link);
    }

    return changedLinks;
}

}
