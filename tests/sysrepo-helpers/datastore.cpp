/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <sysrepo-cpp/Changes.hpp>
#include "datastore.h"
#include "sysrepo-helpers/common.h"

DatastoreWatcher::DatastoreWatcher(sysrepo::Session& session, const std::string& xpath, const std::set<std::string>& ignoredPaths)
    : m_ignoredPaths(ignoredPaths)
    , m_sub(session.onModuleChange(
          moduleFromXpath(xpath),
          [&](sysrepo::Session session, auto, auto, auto, auto, auto) {
              ValueChanges changes;
              for (const auto& change : session.getChanges()) {
                  if (m_ignoredPaths.contains(change.node.schema().path())) {
                      continue;
                  }

                  if (change.node.schema().nodeType() == libyang::NodeType::List) {
                      // any list will surely have some "nodes below", so let's not waste time printing the list entry itself
                      continue;
                  }
                  if (change.node.schema().nodeType() == libyang::NodeType::Container && !change.node.schema().asContainer().isPresence()) {
                      // non-presence containers are "always there", let's not clutter up the output
                      continue;
                  }

                  if (change.operation == sysrepo::ChangeOperation::Deleted) {
                      changes.emplace(change.node.path(), Deleted());
                  } else {
                      changes.emplace(change.node.path(), nodeAsString(change.node));
                  }
              }

              change(changes);
              return sysrepo::ErrorCode::Ok;
          },
          xpath,
          0,
          sysrepo::SubscribeOptions::DoneOnly))
{
}
