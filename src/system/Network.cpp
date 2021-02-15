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
        log->info("Container eth1-standalone not present. Generating bridge configuration for eth1.");
        return {{"eth1", fmt::format(NETWORK_FILE_CONTENT_TEMPLATE, "setting"_a = "Bridge=br0")}};
    } else {
        log->info("Container eth1-standalone is present. Generating DHCP configuration for eth1.");
        return {{"eth1", fmt::format(NETWORK_FILE_CONTENT_TEMPLATE, "setting"_a = "DHCP=yes")}};
    }
}

}

namespace velia::system {

Network::Network(std::shared_ptr<::sysrepo::Connection> srConn, std::filesystem::path runtimeNetworkDirectory, std::function<void(const std::vector<std::string>&)> networkReloadCallback)
    : m_log(spdlog::get("system"))
    , m_srConn(std::move(srConn))
    , m_srSessionRunning(std::make_shared<::sysrepo::Session>(m_srConn, SR_DS_RUNNING))
    , m_srSubscribeRunning(std::make_shared<sysrepo::Subscribe>(m_srSessionRunning))
{
    utils::ensureModuleImplemented(m_srSessionRunning, CZECHLIGHT_SYSTEM_MODULE_NAME, "2021-01-13");

    m_srSubscribeRunning->module_change_subscribe(
        CZECHLIGHT_SYSTEM_MODULE_NAME.c_str(),
        [&, runtimeNetworkDirectory = std::move(runtimeNetworkDirectory), networkReloadCallback = std::move(networkReloadCallback)](sysrepo::S_Session session, [[maybe_unused]] const char* module_name, [[maybe_unused]] const char* xpath, [[maybe_unused]] sr_event_t event, [[maybe_unused]] uint32_t request_id) {
            auto config = getNetworkConfiguration(session, m_log);
            for (const auto& [interface, networkFileContents] : config) {
                velia::utils::safeWriteFile(runtimeNetworkDirectory / (interface + ".network"), networkFileContents);
            }

            std::vector<std::string> interfaces;
            std::transform(config.begin(), config.end(), std::back_inserter(interfaces), [](const auto& kv) { return kv.first; });
            networkReloadCallback(interfaces);

            return SR_ERR_OK;
        },
        CZECHLIGHT_SYSTEM_STANDALONE_ETH1.c_str(),
        0,
        SR_SUBSCR_DONE_ONLY | SR_SUBSCR_ENABLED);
}

}
