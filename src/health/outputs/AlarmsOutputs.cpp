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

namespace {
const auto ietfAlarmsModule = "ietf-alarms";
const auto alarmList = "/ietf-alarms:alarms/alarm-list";
const auto alarmListNode = "/ietf-alarms:alarms/alarm-list/alarm";
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
            /*
             * TODO: read ietf-alarms:alarms/summary when feature alarm-summary implemented
             *       then we can differentiate easily between severities and light the corresponding LED color
             *
             * For now we set output state to ERROR if there is at least one not-cleared error. Otherwise, the state is OK.
             */

            State state = State::OK; // in case no data found or all alarms are cleared

            if (auto data = session.getData(alarmList)) {
                for (const auto& alarmNode : data->findXPath(alarmListNode)) {
                    if (alarmNode.findPath("is-cleared")->asTerm().valueStr() == "true") {
                        continue;
                    }

                    state = State::ERROR;
                    break;
                }
            }
            m_outputSignal(state);

            return sysrepo::ErrorCode::Ok;
        },
        alarmList,
        0,
        sysrepo::SubscribeOptions::Enabled | sysrepo::SubscribeOptions::DoneOnly);
}

AlarmsOutputs::~AlarmsOutputs() = default;
}
