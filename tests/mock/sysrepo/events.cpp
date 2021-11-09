/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "events.h"

namespace {
std::string sr_ev_notif_type_to_string(const sysrepo::NotificationType notif_type)
{
    std::ostringstream oss;
    oss << notif_type;
    return oss.str();
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
        sysrepo::Session,
        uint32_t ,
        const sysrepo::NotificationType type,
        const std::optional<libyang::DataNode> notificationTree,
        const sysrepo::NotificationTimeStamp timestamp)
{
    Event e;
    e.xPath = std::string{notificationTree ? std::string{notificationTree->path()} : "<no-xpath>"};
    e.received = std::chrono::steady_clock::now();
    auto log = spdlog::get("main");
    log->info("SR event {} {} {}", sr_ev_notif_type_to_string(type), timestamp.time_since_epoch().count(), e.xPath);

    if (notificationTree) {
        for (const auto& node : notificationTree->childrenDfs()) {
            auto path = std::string{node.path()};
            auto val = [&] {
                if (node.schema().nodeType() == libyang::NodeType::Leaf) {
                    return std::string{node.asTerm().valueStr()};
                }

                return std::string{""};
            }();
            e.data[path] = val;
            log->debug(" {}: {}", path, val);
        }
    }

    {
        std::lock_guard<std::mutex> lock(*mutex);
        events->push_back(e);
    }

    switch (type) {
    case sysrepo::NotificationType::Realtime:
    case sysrepo::NotificationType::Replay:
        notifRecvCb(e);
        break;
    case sysrepo::NotificationType::ReplayComplete:
    case sysrepo::NotificationType::Terminated:
    case sysrepo::NotificationType::Modified:
    case sysrepo::NotificationType::Suspended:
    case sysrepo::NotificationType::Resumed:
        break;
    }

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
