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

/** @brief Sysrepo daemon listening for changes in alarms from ietf-alarms model.
 *
 * There is a two-step initialization. First, one should create the object.
 * Then set up the signals by connecting them to outputSignal property.
 * After that call activate method which subscribes to sysrepo and starts listening for changes (in another thread).
 */
class AlarmsOutputs {
public:
    AlarmsOutputs(sysrepo::Session session);
    ~AlarmsOutputs();
    void activate();

    ::boost::signals2::signal<void(State)> outputSignal;

private:
    velia::Log m_log;
    sysrepo::Session m_srSession;
    std::optional<sysrepo::Subscription> m_srSubscription;
};

}
