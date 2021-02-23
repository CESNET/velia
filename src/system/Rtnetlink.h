/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include <functional>
#include <netlink/netlink.h>
#include <stdexcept>
#include "utils/log-fwd.h"

namespace velia::system {

class RtnetlinkException : public std::runtime_error {
public:
    RtnetlinkException(const std::string& error)
        : std::runtime_error("Rtnetlink call " + error + " failed")
    {
    }
};

class Rtnetlink {
public:
    struct LinkInfo {
        std::string m_name;
        std::string m_physAddr;
        uint8_t m_operStatus;
    };

    Rtnetlink();
    virtual std::vector<LinkInfo> links() const;

private:
    std::unique_ptr<nl_sock, std::function<void(nl_sock*)>> m_nlSocket;
    velia::Log m_log;
};

}
