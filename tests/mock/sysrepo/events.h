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
    ~EventWatcher();
    void operator()(::sysrepo::S_Session session, const sr_ev_notif_type_t notif_type, const char* xpath, const ::sysrepo::S_Vals vals, time_t timestamp);
    void operator()(::sysrepo::S_Session session, const sr_ev_notif_type_t notif_type, libyang::S_Data_Node notif, time_t timestamp);

    struct Event {
        std::string xPath;
        std::map<std::string, std::string> data;
        std::chrono::time_point<std::chrono::steady_clock> received;
    };

    Event peek(std::vector<Event>::size_type index) const;
    std::vector<Event>::size_type count() const;

private:
    mutable std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::vector<Event>> events = std::make_shared<std::vector<Event>>();
};
