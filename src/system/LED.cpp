/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "LED.h"

#include <utility>
#include "utils/io.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;

namespace {

const auto CZECHLIGHT_SYSTEM_MODULE_NAME = "czechlight-system"s;
const auto CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX = "/"s + CZECHLIGHT_SYSTEM_MODULE_NAME + ":leds/"s;

}

namespace velia::system {

LED::LED(const std::shared_ptr<::sysrepo::Connection>& srConn, std::filesystem::path sysfsLeds)
    : m_log(spdlog::get("system"))
    , m_sysfsLeds(std::move(sysfsLeds))
    , m_srSession(std::make_shared<::sysrepo::Session>(srConn))
    , m_srSubscribe(std::make_shared<::sysrepo::Subscribe>(m_srSession))
{
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_SYSTEM_MODULE_NAME, "2021-01-13");

    m_srSubscribe->oper_get_items_subscribe(
        CZECHLIGHT_SYSTEM_MODULE_NAME.c_str(),
        [this](auto session, auto, auto, auto, auto, auto& parent) {
            std::map<std::string, std::string> data;

            for (const auto& entry : std::filesystem::directory_iterator(m_sysfsLeds)) {
                if (!std::filesystem::is_directory(entry.path())) {
                    continue;
                }

                const std::string deviceName = entry.path().filename();

                try {
                    /* actually just uint32_t is needed for the next two variables; but there is no harm in reading them as int64_t and downcasting them later (especially when the code for reading int64_t already exists)
                     * See https://github.com/torvalds/linux/commit/af0bfab907a011e146304d20d81dddce4e4d62d0
                     */
                    const auto brightness = velia::utils::readFileInt64(entry.path() / "brightness");
                    const auto max_brightness = velia::utils::readFileInt64(entry.path() / "max_brightness");
                    auto percent = brightness * 100 / max_brightness;
                    m_log->trace("Found LED '{}' with brightness of {} % (brightness {} out of {})", deviceName, percent, brightness, max_brightness);

                    data[CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "led[name='" + deviceName + "']/brightness"] = std::to_string(percent);
                } catch (const std::invalid_argument& e) {
                    m_log->warn("Failed reading state of the LED '{}': {}", deviceName, e.what());
                }
            }

            utils::valuesToYang(data, {}, session, parent);
            return SR_ERR_OK;
        },
        (CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "*").c_str(),
        SR_SUBSCR_PASSIVE | SR_SUBSCR_OPER_MERGE | SR_SUBSCR_CTX_REUSE);
}

}
