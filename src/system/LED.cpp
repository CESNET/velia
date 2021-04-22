/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "LED.h"

#include <utility>
#include "utils/io.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::literals;

namespace {

const auto CZECHLIGHT_SYSTEM_MODULE_NAME = "czechlight-system"s;
const auto CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX = "/"s + CZECHLIGHT_SYSTEM_MODULE_NAME + ":leds/"s;

const auto UID_LED = "uid:blue"s;
const auto POLL_INTERVAL = 125ms;
}

namespace velia::system {

LED::LED(const std::shared_ptr<::sysrepo::Connection>& srConn, std::filesystem::path sysfsLeds)
    : m_log(spdlog::get("system"))
    , m_sysfsLeds(std::move(sysfsLeds))
    , m_srSession(std::make_shared<::sysrepo::Session>(srConn))
    , m_srSubscribe(std::make_shared<::sysrepo::Subscribe>(m_srSession))
    , m_thrRunning(true)
{
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_SYSTEM_MODULE_NAME, "2021-01-13");

    for (const auto& entry : std::filesystem::directory_iterator(m_sysfsLeds)) {
        if (!std::filesystem::is_directory(entry.path())) {
            continue;
        }

        m_leds.push_back(entry.path().filename());
    }

    m_thr = std::thread(&LED::poll, this);

    const auto uidMaxBrightness = std::to_string(velia::utils::readOneFromFile<uint32_t>(m_sysfsLeds / UID_LED / "max_brightness"));
    const auto triggerFile = m_sysfsLeds / UID_LED / "trigger";
    const auto brightnessFile = m_sysfsLeds / UID_LED / "brightness";

    m_srSubscribe->rpc_subscribe_tree(
        (CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "uid").c_str(),
        [this, uidMaxBrightness, triggerFile, brightnessFile](auto session, auto, auto input, auto, auto, auto) {
            std::string val = getValueAsString(getSubtree(input, (CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "uid/state").c_str()));

            try {
                if (val == "on") {
                    utils::writeFile(triggerFile, "none");
                    utils::writeFile(brightnessFile, uidMaxBrightness);
                } else if (val == "off") {
                    utils::writeFile(triggerFile, "none"); // setting trigger to none also clears brightness settings
                } else if (val == "blinking") {
                    utils::writeFile(triggerFile, "timer");
                    utils::writeFile(brightnessFile, uidMaxBrightness);
                }
            } catch (const std::invalid_argument& e) {
                m_log->warn("Failed to set state of the UID LED: '{}'", e.what());
                session->set_error("Failed to set state of the UID LED", nullptr);
                return SR_ERR_OPERATION_FAILED;
            }

            return SR_ERR_OK;
        });
}

LED::~LED()
{
    m_thrRunning = false;
    m_thr.join();
}

void LED::poll() const
{
    while (m_thrRunning) {
        std::map<std::string, std::string> data;

        for (const auto& ledDirectory : m_leds) {
            try {
                const auto brightness = velia::utils::readOneFromFile<uint32_t>(m_sysfsLeds / ledDirectory / "brightness");
                const auto max_brightness = velia::utils::readOneFromFile<uint32_t>(m_sysfsLeds / ledDirectory / "max_brightness");
                auto percent = brightness * 100 / max_brightness;

                data[CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "led[name='" + std::string(ledDirectory) + "']/brightness"] = std::to_string(percent);
            } catch (const std::invalid_argument& e) {
                m_log->warn("Failed reading state of the LED '{}': {}", std::string(ledDirectory), e.what());
            }
        }

        utils::valuesPush(data, {}, m_srSession, SR_DS_OPERATIONAL);

        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}

}
