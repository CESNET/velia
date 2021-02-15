/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <fmt/core.h>
#include <sstream>
#include "Network.h"
#include "system_vars.h"
#include "utils/exec.h"
#include "utils/io.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;
using namespace fmt::literals;

namespace {

const auto CZECHLIGHT_SYSTEM_MODULE_NAME = "czechlight-system"s;
const auto CZECHLIGHT_SYSTEM_NOBRIDGE_PREFIX = "/"s + CZECHLIGHT_SYSTEM_MODULE_NAME + ":networking/eth1-ip"s;

const auto NETWORK_FILE_CONTENTS_TEMPLATE = "[Match]\n"
                                            "Name=eth1\n\n"
                                            "[Network]\n"
                                            "{setting}\n"
                                            "LLDP=true\n"
                                            "EmitLLDP=nearest-bridge\n";

const auto NETWORK_FILENAME = "eth1.network";

enum class NetworkConfiguration
{
    BRIDGE,
    STATIC_ADDRESS,
    DHCP,
};

std::pair<NetworkConfiguration, std::optional<std::string>> getNetworkConfiguration(std::shared_ptr<::sysrepo::Session> session, velia::Log log)
{
    auto networkContainer = session->get_data(CZECHLIGHT_SYSTEM_NOBRIDGE_PREFIX.c_str());

    if (networkContainer == nullptr) {
        log->info("Configuration for eth1 not found. Determining network bridge configuration.");
        return std::make_pair(NetworkConfiguration::BRIDGE, std::nullopt);
    } else if (auto static_ip_node_set = networkContainer->find_path((CZECHLIGHT_SYSTEM_NOBRIDGE_PREFIX + "/static-ip-address").c_str()); static_ip_node_set != nullptr && static_ip_node_set->number() == 1) {
        auto ip = getValueAsString(static_ip_node_set->data()[0]);
        log->info("Configuration for eth1 found: Static address {}", ip);
        return std::make_pair(NetworkConfiguration::STATIC_ADDRESS, ip);
    } else {
        log->info("Configuration for eth1 found: DHCP");
        return std::make_pair(NetworkConfiguration::DHCP, std::nullopt);
    }
}

std::string createNetworkFile(const NetworkConfiguration& conf, const std::optional<std::string>& ip)
{
    switch(conf) {
    case NetworkConfiguration::BRIDGE:
        return fmt::format(NETWORK_FILE_CONTENTS_TEMPLATE, "setting"_a="Bridge=br0");
    case NetworkConfiguration::STATIC_ADDRESS:
        return fmt::format(NETWORK_FILE_CONTENTS_TEMPLATE, "setting"_a="Address=" + *ip);
    case NetworkConfiguration::DHCP:
        return fmt::format(NETWORK_FILE_CONTENTS_TEMPLATE, "setting"_a="DHCP=yes");
    }

    __builtin_unreachable();
}

}

namespace velia::system {

Network::Network(std::shared_ptr<::sysrepo::Connection> srConn, const std::filesystem::path& runtimeNetworkDirectory)
    : m_log(spdlog::get("system"))
    , m_srConn(std::move(srConn))
    , m_srSessionRunning(std::make_shared<::sysrepo::Session>(m_srConn, SR_DS_RUNNING))
    , m_srSubscribeRunning(std::make_shared<sysrepo::Subscribe>(m_srSessionRunning))
{
    utils::ensureModuleImplemented(m_srSessionRunning, CZECHLIGHT_SYSTEM_MODULE_NAME, "2021-01-13");

    auto cb = [&, runtimeNetworkDirectory](sysrepo::S_Session session, [[maybe_unused]] const char *module_name, [[maybe_unused]] const char *xpath, [[maybe_unused]] sr_event_t event, [[maybe_unused]] uint32_t request_id)
    {
        auto networkConf = getNetworkConfiguration(session, m_log);
        velia::utils::safeWriteFile(runtimeNetworkDirectory / NETWORK_FILENAME, std::apply(createNetworkFile, networkConf));

        // change in running datastore: just reload network configuration
        velia::utils::execAndWait(m_log, NETWORKCTL_EXECUTABLE, {"reload"}, "");
        if (networkConf.first == NetworkConfiguration::DHCP || networkConf.first == NetworkConfiguration::STATIC_ADDRESS) {
            velia::utils::execAndWait(m_log, IPROUTE2_IP_EXECUTABLE, {"link", "set", "eth1", "nomaster"}, "");
        } else {
            velia::utils::execAndWait(m_log, IPROUTE2_IP_EXECUTABLE, {"link", "set", "eth1", "master", "br0"}, "");
        }

        return SR_ERR_OK;
    };

    m_srSubscribeRunning->module_change_subscribe(CZECHLIGHT_SYSTEM_MODULE_NAME.c_str(), cb, CZECHLIGHT_SYSTEM_NOBRIDGE_PREFIX.c_str(), 0, SR_SUBSCR_DONE_ONLY | SR_SUBSCR_ENABLED);
}

}
