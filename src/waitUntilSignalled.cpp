/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 */

#include <csignal>
#include <spdlog/spdlog.h>
#include <unistd.h>
#include "waitUntilSignalled.h"

void waitUntilSignaled()
{
    signal(SIGTERM, [](int) {});
    signal(SIGINT, [](int) {});
    pause();
    spdlog::get("main")->debug("Shutting down");
}
