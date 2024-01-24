/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <sysrepo-cpp/Subscription.hpp>
#include "trompeloeil_doctest.h"
#include "test_log_setup.h"
#include "sysrepo-helpers/common.h"

/** @short Watch for a given YANG notification

When a real-time notification is recieved, the `notified()` method is invoked with stringified values
of all terminals that were passed to the original notification.
*/
struct NotificationWatcher {
    NotificationWatcher(sysrepo::Session& session, const std::string& xpath);
    MAKE_MOCK1(notified, void(const Values&));
private:
    sysrepo::Subscription m_sub;
};
