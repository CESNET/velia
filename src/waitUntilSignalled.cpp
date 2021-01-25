#include <csignal>
#include "main.h"

void waitUntilSignaled()
{
    signal(SIGTERM, [](int) {});
    signal(SIGINT, [](int) {});
    pause();
    spdlog::get("main")->debug("Shutting down");
}
