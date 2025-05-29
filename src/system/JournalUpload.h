/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include <sysrepo-cpp/Subscription.hpp>
#include "utils/log-fwd.h"

namespace velia::system {

class JournalUpload {
public:
    using RestartCb = std::function<void(velia::Log)>;

    JournalUpload(sysrepo::Session session, const std::filesystem::path& envFile, const RestartCb& restartCb);

private:
    velia::Log m_log;
    sysrepo::Subscription m_srSub;
};

}
