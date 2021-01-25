#pragma once

#include <csignal>
#include <sdbus-c++/sdbus-c++.h>
#include <thread>
#include "utils/log.h"

#define DBUS_EVENTLOOP_INIT                               \
    std::unique_ptr<sdbus::IConnection> g_dbusConnection; \
    std::thread g_eventLoop;

#define DBUS_EVENTLOOP_START                                               \
    spdlog::get("main")->debug("Opening DBus connection");                 \
    g_dbusConnection = sdbus::createSystemBusConnection();                 \
    /* Gracefully leave dbus event loop on SIGTERM */                      \
    struct sigaction sigact;                                               \
    memset(&sigact, 0, sizeof(sigact));                                    \
    /* sdbus-c++'s implementation doesn't mind if called before entering the event loop. It simply leaves the loop on entry */ \
    sigact.sa_flags = SA_SIGINFO;                                          \
    sigact.sa_handler = [](int) { g_dbusConnection->leaveEventLoop(); };   \
    sigaction(SIGTERM, &sigact, nullptr);                                  \
    spdlog::get("main")->debug("Starting DBus event loop");                \
    g_eventLoop = std::thread([] { g_dbusConnection->enterEventLoop(); }); \

#define DBUS_EVENTLOOP_END \
    g_eventLoop.join();      \
    spdlog::get("main")->debug("Shutting down");

#define SIMPLE_DAEMONIZE_INIT \
    volatile sig_atomic_t g_exit_application = 0;

#define SIMPLE_DAEMONIZE                                                                                                                  \
    /* Install sighandler for SIGTERM */                                                                                           \
    struct sigaction sigact;                                                                                                       \
    memset(&sigact, 0, sizeof(sigact));                                                                                            \
    sigact.sa_handler = [](int) { g_exit_application = 1; };                                                                       \
    sigact.sa_flags = SA_SIGINFO;                                                                                                  \
    sigaction(SIGTERM, &sigact, nullptr);                                                                                          \
                                                                                                                                   \
    /*  Block SIGTERM */                                                                                                           \
    sigset_t sigset, oldset;                                                                                                       \
    sigemptyset(&sigset);                                                                                                          \
    sigaddset(&sigset, SIGTERM);                                                                                                   \
    sigprocmask(SIG_BLOCK, &sigset, &oldset);                                                                                      \
                                                                                                                                   \
    while (!g_exit_application) {                                                                                                  \
        fd_set fd;                                                                                                                 \
        FD_ZERO(&fd);                                                                                                              \
                                                                                                                                   \
        /* if SIGTERM received at this point, it is deffered until pselect is entered which enables the signal processing again */ \
        pselect(0, &fd, NULL, NULL, NULL, &oldset);                                                                                \
    }                                                                                                                              \
    spdlog::get("main")->debug("Shutting down");
