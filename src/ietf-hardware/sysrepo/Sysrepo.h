/*
 * Copyright (C) 2020-2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <chrono>
#include <memory>
#include <sysrepo-cpp/Subscription.hpp>
#include <thread>
#include "ietf-hardware/IETFHardware.h"
#include "utils/log-fwd.h"

namespace velia::ietf_hardware::sysrepo {

/** @class Sysrepo
 *  A callback class for operational data in Sysrepo. This class expects a shared_pointer<HardwareState> instance.
 *  It asks HardwareState instance for the hardware state data every @p pollInterval interval and it pushes them into Sysrepo.
 *
 *  @see velia::ietf_hardware::IETFHardware
 */
class Sysrepo {
public:
    Sysrepo(::sysrepo::Session session, std::shared_ptr<IETFHardware> driver, std::chrono::microseconds pollInterval);
    ~Sysrepo();

private:
    velia::Log m_log;
    std::chrono::microseconds m_pollInterval;
    ::sysrepo::Session m_session;
    std::optional<::sysrepo::Subscription> m_assetSub;
    std::shared_ptr<IETFHardware> m_hwState;
    std::atomic<bool> m_quit;
    std::thread m_pollThread;
};
}
