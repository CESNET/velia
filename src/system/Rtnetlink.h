/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include <functional>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <stdexcept>
#include "utils/log-fwd.h"

namespace velia::system {

/**
 * @brief Readout of values via netlink protocol.
 */
class Rtnetlink {
public:
    Rtnetlink();
    void iterLinks(const std::function<void(rtnl_link*)> &cb) const;

private:
    std::unique_ptr<nl_sock, std::function<void(nl_sock*)>> m_nlSocket;
    velia::Log m_log;
};

class RtnetlinkException : public std::runtime_error {
public:
    RtnetlinkException(const std::string& funcName);
};

}
