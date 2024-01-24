/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "events.h"
#include "sysrepo-helpers/common.h"

NotificationWatcher::NotificationWatcher(sysrepo::Session& session, const std::string& xpath)
    : m_sub{session.onNotification(moduleFromXpath(xpath),
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
