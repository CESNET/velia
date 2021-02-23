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

/**
 * @brief Readout of values via netlink protocol.
 */
class Rtnetlink {
public:
    struct LinkInfo {
        std::string m_name;
        std::string m_physAddr;
        uint8_t m_operStatus;
    };

    Rtnetlink();
    std::vector<LinkInfo> links() const;

private:
    std::unique_ptr<nl_sock, std::function<void(nl_sock*)>> m_nlSocket;
    velia::Log m_log;
};

class RtnetlinkException : public std::runtime_error {
public:
    RtnetlinkException(const std::string& funcName);
};

}
