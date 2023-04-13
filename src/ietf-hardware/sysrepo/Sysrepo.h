/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sysrepo-cpp/Session.hpp>
#include <thread>
#include "ietf-hardware/IETFHardware.h"
#include "utils/log-fwd.h"

namespace velia::ietf_hardware::sysrepo {

/** @class Sysrepo
 *  A callback class for operational data in Sysrepo. This class expects a shared_pointer<HardwareState> instance.
 *  It periodically asks HardwareState instance for the hardware state data and it pushes them into Sysrepo.
 *
 *  @see velia::ietf_hardware::IETFHardware
 */
class Sysrepo {
public:
    Sysrepo(::sysrepo::Session session, std::shared_ptr<IETFHardware> driver);
    ~Sysrepo();

private:
    velia::Log m_Log;
    ::sysrepo::Session m_session;
    std::shared_ptr<IETFHardware> m_hwState;
    std::atomic<bool> m_quit;
    std::thread m_pollThread;
};
}
