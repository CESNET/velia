/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <chrono>
#include <mutex>
#include <sysrepo-cpp/Session.hpp>
#include "test_log_setup.h"

class EventWatcher {
public:
    struct Event {
        std::string xPath;
        std::map<std::string, std::string> data;
        std::chrono::time_point<std::chrono::steady_clock> received;
    };

    explicit EventWatcher(std::function<void(Event)> callback);
    ~EventWatcher();
    void operator()(sysrepo::Session session, uint32_t subscriptionId, const sysrepo::NotificationType type, const std::optional<libyang::DataNode> notificationTree, const sysrepo::NotificationTimeStamp timestamp);

    Event peek(std::vector<Event>::size_type index) const;
    std::vector<Event>::size_type count() const;

private:
    std::function<void(Event)> notifRecvCb;
    mutable std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::vector<Event>> events = std::make_shared<std::vector<Event>>();
};
