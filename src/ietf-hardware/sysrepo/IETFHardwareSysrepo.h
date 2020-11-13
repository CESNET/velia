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

class IETFHardwareSysrepo {
public:
    IETFHardwareSysrepo(std::shared_ptr<::sysrepo::Subscribe> srSubscribe, std::shared_ptr<IETFHardware> driver);

private:
    std::shared_ptr<IETFHardware> m_hwState;
    std::shared_ptr<::sysrepo::Subscribe> m_srSubscribe;
    uint64_t m_srLastRequestId;
};
}
