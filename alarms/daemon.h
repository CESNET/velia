#pragma once
#include <sysrepo-cpp/Connection.hpp>

sysrepo::ErrorCode mngrUpdateControlCb(sysrepo::Session session, sysrepo::Event event, sysrepo::Connection& dataConn, sysrepo::Session& dataSess);
sysrepo::ErrorCode mngrRPC(sysrepo::Session session, sysrepo::Event event, const libyang::DataNode input, libyang::DataNode output, sysrepo::Connection& dataConn, sysrepo::Session& dataSess);
