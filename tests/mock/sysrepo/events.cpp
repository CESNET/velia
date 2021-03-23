/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "events.h"

namespace {
std::string sr_ev_notif_type_to_string(const sr_ev_notif_type_t notif_type)
{
    switch (notif_type) {
    case SR_EV_NOTIF_REALTIME:
        return "SR_EV_NOTIF_REALTIME";
    case SR_EV_NOTIF_REPLAY:
        return "SR_EV_NOTIF_REPLAY";
    case SR_EV_NOTIF_REPLAY_COMPLETE:
        return "SR_EV_NOTIF_REPLAY_COMPLETE";
    case SR_EV_NOTIF_STOP:
        return "SR_EV_NOTIF_STOP";
    default:
        return "[unknown event type]";
    }
}
}

EventWatcher::EventWatcher(std::function<void(Event)> callback)
    : notifRecvCb(std::move(callback))
{
}

EventWatcher::~EventWatcher()
{
}

void EventWatcher::operator()(
    [[maybe_unused]] ::sysrepo::S_Session session,
    const sr_ev_notif_type_t notif_type,
    const char* xpath,
    const ::sysrepo::S_Vals vals,
    time_t timestamp)
{
    Event e;
    e.xPath = xpath;
    e.received = std::chrono::steady_clock::now();

    auto log = spdlog::get("main");
    log->info("SR event {} {} {}", sr_ev_notif_type_to_string(notif_type), timestamp, xpath);

    for (size_t i = 0; i < vals->val_cnt(); ++i) {
        const auto& v = vals->val(i);
        auto s = v->val_to_string();
        log->debug(" {}: {}", v->xpath(), s);
        e.data[v->xpath()] = s;
    }

    {
        std::lock_guard<std::mutex> lock(*mutex);
        events->push_back(e);
    }

    notifRecvCb(e);
}

std::vector<EventWatcher::Event>::size_type EventWatcher::count() const
{
    std::lock_guard<std::mutex> lock(*mutex);
    return events->size();
}

EventWatcher::Event EventWatcher::peek(const std::vector<EventWatcher::Event>::size_type index) const
{
    std::lock_guard<std::mutex> lock(*mutex);
    return (*events)[index];
}
