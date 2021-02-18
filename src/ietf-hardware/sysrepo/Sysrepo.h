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
#include "ietf-hardware/IETFHardware.h"

namespace velia::ietf_hardware::sysrepo {

/** @class Sysrepo
 *  A callback class for operational data in Sysrepo. This class expects a shared_pointer<HardwareState> instance.
 *  When sysrepo callbacks for the data in the subtree this callback is registered for, it asks HardwareState instace
 *  for the data it should return back to Sysrepo.
 *  OpsCallback then creates the YANG tree structure from the data returned by HardwareState and returns it.
 *
 *  @see velia::ietf_hardware::IETFHardware
 */
class Sysrepo {
public:
    Sysrepo(std::shared_ptr<::sysrepo::Subscribe> srSubscribe, std::shared_ptr<IETFHardware> driver);

private:
    std::shared_ptr<IETFHardware> m_hwState;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
};
}
