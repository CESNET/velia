#pragma once

#include <csignal>
#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include "utils/log.h"

#define DBUS_EVENTLOOP_INIT                               \
    std::unique_ptr<sdbus::IConnection> g_dbusConnection; \
    std::thread g_eventLoop;

#define DBUS_EVENTLOOP_START                                               \
    g_dbusConnection = sdbus::createSystemBusConnection();                 \
    /* Gracefully leave dbus event loop on SIGTERM */                      \
    struct sigaction sigact;                                               \
    memset(&sigact, 0, sizeof(sigact));                                    \
    /* sdbus-c++'s implementation doesn't mind if called before entering the event loop. It simply leaves the loop on entry */ \
    sigact.sa_flags = SA_SIGINFO;                                          \
    sigact.sa_handler = [](int) { g_dbusConnection->leaveEventLoop(); };   \
    sigaction(SIGTERM, &sigact, nullptr);                                  \
    g_eventLoop = std::thread([] { g_dbusConnection->enterEventLoop(); }); \

#define DBUS_EVENTLOOP_END \
    g_eventLoop.join();
