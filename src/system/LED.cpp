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
    , m_srSession(std::make_shared<::sysrepo::Session>(srConn))
    , m_srSubscribe(std::make_shared<::sysrepo::Subscribe>(m_srSession))
    , m_thrRunning(true)
{
    utils::ensureModuleImplemented(m_srSession, CZECHLIGHT_SYSTEM_MODULE_NAME, "2021-01-13");

    for (const auto& entry : std::filesystem::directory_iterator(sysfsLeds)) {
        if (!std::filesystem::is_directory(entry.path())) {
            continue;
        }

        const auto fullPath = sysfsLeds / entry.path();
        m_log->debug("Discovered LED '{}' (max brightness {})", std::string(entry.path().filename()), velia::utils::readFileInt64(fullPath / "max_brightness"));
        m_leds.push_back(fullPath);
    }

    m_thr = std::thread(&LED::poll, this);

    const auto uidMaxBrightness = std::to_string(velia::utils::readFileInt64(sysfsLeds / UID_LED / "max_brightness"));
    const auto triggerFile = sysfsLeds / UID_LED / "trigger";
    const auto brightnessFile = sysfsLeds / UID_LED / "brightness";

    m_srSubscribe->rpc_subscribe_tree(
        (CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "uid").c_str(),
        [this, uidMaxBrightness, triggerFile, brightnessFile](auto session, auto, auto input, auto, auto, auto) {
            std::string val = getValueAsString(getSubtree(input, (CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "uid/state").c_str()));

            try {
                if (val == "on") {
                    utils::writeFile(triggerFile, "none");
                    utils::writeFile(brightnessFile, uidMaxBrightness);
                } else if (val == "off") {
                    utils::writeFile(triggerFile, "none");
                    utils::writeFile(brightnessFile, "0");
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
            const auto deviceName = ledDirectory.filename();

            try {
                /* actually just uint32_t is needed for the next two variables; but there is no harm in reading them as int64_t and downcasting them later (especially when the code for reading int64_t already exists)
                 * See https://github.com/torvalds/linux/commit/af0bfab907a011e146304d20d81dddce4e4d62d0
                 */
                const uint32_t brightness = velia::utils::readFileInt64(ledDirectory / "brightness");
                const uint32_t max_brightness = velia::utils::readFileInt64(ledDirectory / "max_brightness");
                auto percent = brightness * 100 / max_brightness;

                data[CZECHLIGHT_SYSTEM_LEDS_MODULE_PREFIX + "led[name='" + std::string(deviceName) + "']/brightness"] = std::to_string(percent);
            } catch (const std::invalid_argument& e) {
                m_log->warn("Failed reading state of the LED '{}': {}", std::string(deviceName), e.what());
            }
        }

        utils::valuesPush(data, {}, m_srSession, SR_DS_OPERATIONAL);

        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}
}
