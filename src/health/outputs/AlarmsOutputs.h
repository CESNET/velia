/*
 * Copyright (C) 2020 - 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <boost/signals2/signal.hpp>
#include <optional>
#include <sysrepo-cpp/Subscription.hpp>
#include <vector>
#include "health/State.h"
#include "utils/log-fwd.h"

namespace velia::health {

/** @brief Sysrepo subscription listening for changes in alarms from ietf-alarms model. */
class AlarmsOutputs {
public:
    AlarmsOutputs(sysrepo::Session session, const std::vector<std::function<void(State)>>& outputHandlers);
    ~AlarmsOutputs();

private:
    ::boost::signals2::signal<void(State)> m_outputSignal;
    velia::Log m_log;
    sysrepo::Session m_srSession;
    std::optional<sysrepo::Subscription> m_srSubscription;
};

}
