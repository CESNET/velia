/*
 * Copyright (C) 2020 - 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <boost/signals2/signal.hpp>
#include <optional>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Subscription.hpp>
#include <vector>
#include "health/State.h"
#include "utils/log-fwd.h"

namespace velia::health {

class AlarmsOutputs {
public:
    AlarmsOutputs(sysrepo::Session session, const std::vector<std::function<void(State)>>& signals);
    ~AlarmsOutputs();

    ::boost::signals2::signal<void(State)> outputSignal;

private:
    velia::Log m_log;
    sysrepo::Session m_srSession;
    std::optional<sysrepo::Subscription> m_srSubscription;
};

}
