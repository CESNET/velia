/*
 * Copyright (C) 2020 - 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#include <string>
#include <sysrepo-cpp/Enum.hpp>
#include "AlarmsOutputs.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;

namespace {
const auto ietfAlarmsModule = "ietf-alarms";
const auto alarmSummary = "/ietf-alarms:alarms/summary";
}

namespace velia::health {

AlarmsOutputs::AlarmsOutputs(sysrepo::Session session, const std::vector<std::function<void(State)>>& outputHandlers)
    : m_log(spdlog::get("health"))
    , m_srSession(std::move(session))
{
    utils::ensureModuleImplemented(m_srSession, "sysrepo-ietf-alarms", "2022-02-17");

    for (const auto& f : outputHandlers) {
        m_outputSignal.connect(f);
    }

    m_srSubscription = m_srSession.onModuleChange(
        ietfAlarmsModule,
        [&](sysrepo::Session session, auto, auto, auto, auto, auto) {
            State state = State::OK; // in case no uncleared alarms found

            if (auto data = session.getData(alarmSummary)) {
                for (const auto& [severity, errorState] : {std::pair<const char*, State>{"indeterminate", State::WARNING}, {"warning", State::WARNING}, {"minor", State::ERROR}, {"major", State::ERROR}, {"critical", State::ERROR}}) {
                    const auto activeAlarms = std::stoi(std::string(data->findPath(alarmSummary + "/alarm-summary[severity='"s + severity + "']/not-cleared")->asTerm().valueStr()));

                    if (activeAlarms > 0) {
                        state = errorState;
                    }
                }
            }
            m_outputSignal(state);

            return sysrepo::ErrorCode::Ok;
        },
        alarmSummary,
        0,
        sysrepo::SubscribeOptions::Enabled | sysrepo::SubscribeOptions::DoneOnly);
}

AlarmsOutputs::~AlarmsOutputs() = default;
}
