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

EventWatcher::~EventWatcher()
{
}

void EventWatcher::operator()(
        [[maybe_unused]] ::sysrepo::S_Session session,
        const sr_ev_notif_type_t notif_type,
        const char *xpath,
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

    std::lock_guard<std::mutex> lock(*mutex);
    events->push_back(e);
}

void EventWatcher::operator()(
        [[maybe_unused]] ::sysrepo::S_Session session,
        const sr_ev_notif_type_t notif_type,
        const ::libyang::S_Data_Node notif,
        time_t timestamp)
{
    Event e;
    e.xPath = notif->path();
    e.received = std::chrono::steady_clock::now();

    auto log = spdlog::get("main");
    log->info("SR event {} {} {}", sr_ev_notif_type_to_string(notif_type), timestamp, notif->path());

    struct lyd_node *elem = nullptr, *next = nullptr;
    LY_TREE_DFS_BEGIN(notif->C_lyd_node(), next, elem) {
        auto nodeType = elem->schema->nodetype;
        if (nodeType == LYS_LEAFLIST || nodeType == LYS_LEAF) {
            auto xpath = std::unique_ptr<char, decltype(std::free) *>{lyd_path(elem), std::free};
            std::string data = reinterpret_cast<struct ::lyd_node_leaf_list*>(elem)->value_str;
            log->debug(" {}: {}", xpath.get(), data);
            e.data[xpath.get()] = data;
        }
        LY_TREE_DFS_END(notif->C_lyd_node(), next, elem)
    }

    std::lock_guard<std::mutex> lock(*mutex);
    events->push_back(e);
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
