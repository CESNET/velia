#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include "utils/log-init.h"

int main()
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();

    cla::utils::initLogs(loggingSink);
    spdlog::set_level(spdlog::level::info);

    spdlog::info("Velia");
}
