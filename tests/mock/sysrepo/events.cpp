/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "events.h"

NotificationWatcher::NotificationWatcher(sysrepo::Session& session, const std::string& xpath)
    : m_sub{session.onNotification(
                [xpath]() {
                    auto pos = xpath.find(":");
                    if (pos == 0 || pos == std::string::npos) {
                        throw std::logic_error{"NotificationWatcher: Malformed XPath " + xpath};
                    }
                    return xpath.substr(1, pos - 1);
                }(),
                [this, xpath](sysrepo::Session, uint32_t, const sysrepo::NotificationType type, const std::optional<libyang::DataNode> tree, const sysrepo::NotificationTimeStamp) {
                    if (type != sysrepo::NotificationType::Realtime) {
                        return;
                    }
                    data_t data;
                    for (const auto& it : tree->findPath(xpath)->childrenDfs()) {
                        if (!it.isTerm()) {
                            continue;
                        }
                        data[it.path().substr(xpath.size() + 1 /* trailing slash */)] = std::visit(libyang::ValuePrinter{}, it.asTerm().value());
                    }
                    notified(data);
                },
                xpath)}
{
}

