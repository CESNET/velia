/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <fmt/core.h>
#include "Network.h"
#include "utils/io.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;
using namespace fmt::literals;

namespace {

const auto CZECHLIGHT_SYSTEM_MODULE_NAME = "czechlight-system"s;
const auto CZECHLIGHT_SYSTEM_STANDALONE_ETH1 = "/"s + CZECHLIGHT_SYSTEM_MODULE_NAME + ":networking/standalone-eth1"s;

const std::string NETWORK_FILE_CONTENT_TEMPLATE = R"([Match]
Name=eth1

[Network]
{setting}
LLDP=true
EmitLLDP=nearest-bridge
)";

std::map<std::string, std::string> getNetworkConfiguration(std::shared_ptr<::sysrepo::Session> session, velia::Log log)
{
    if (session->get_data(CZECHLIGHT_SYSTEM_STANDALONE_ETH1.c_str()) == nullptr) { // the presence container is missing, bridge eth1
        log->debug("Container eth1-standalone not present. Generating bridge configuration for eth1.");
        return {{"eth1", fmt::format(NETWORK_FILE_CONTENT_TEMPLATE, "setting"_a = "Bridge=br0")}};
    } else {
        log->debug("Container eth1-standalone is present. Generating DHCPv6 configuration for eth1.");
        return {{"eth1", fmt::format(NETWORK_FILE_CONTENT_TEMPLATE, "setting"_a = "DHCP=ipv6")}};
    }
}

}

namespace velia::system {

Network::Network(std::shared_ptr<::sysrepo::Session> srSess, std::filesystem::path networkConfigDirectory, std::function<void(const std::vector<std::string>&)> networkReloadCallback)
    : m_log(spdlog::get("system"))
    , m_srSubscribe(std::make_shared<sysrepo::Subscribe>(srSess))
{
    utils::ensureModuleImplemented(srSess, CZECHLIGHT_SYSTEM_MODULE_NAME, "2021-01-13");

    m_srSubscribe->module_change_subscribe(
        CZECHLIGHT_SYSTEM_MODULE_NAME.c_str(),
        [&, networkConfigDirectory = std::move(networkConfigDirectory), networkReloadCallback = std::move(networkReloadCallback)](sysrepo::S_Session session, [[maybe_unused]] const char* module_name, [[maybe_unused]] const char* xpath, [[maybe_unused]] sr_event_t event, [[maybe_unused]] uint32_t request_id) {
            m_log->trace("DS {}: event", session->session_get_ds());
            auto config = getNetworkConfiguration(session, m_log);
            std::vector<std::string> changedInterfaces;

            for (const auto& [interface, networkFileContents] : config) {
                auto targetFile = networkConfigDirectory / (interface + ".network");

                if (!std::filesystem::exists(targetFile) || velia::utils::readFileToString(targetFile) != networkFileContents) { // don't reload if the new file same as the already existing file
                    m_log->trace("Will write out {}: {}", targetFile, networkFileContents);
                    velia::utils::safeWriteFile(targetFile, networkFileContents);
                    changedInterfaces.push_back(interface);
                }
            }

            networkReloadCallback(changedInterfaces);

            return SR_ERR_OK;
        },
        CZECHLIGHT_SYSTEM_STANDALONE_ETH1.c_str(),
        0,
        SR_SUBSCR_DONE_ONLY | SR_SUBSCR_ENABLED);
}

}
