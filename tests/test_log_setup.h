/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#pragma once

#include <iostream>
#include <spdlog/sinks/ansicolor_sink.h>
#include "utils/log-init.h"
#include "utils/sysrepo.h"
#include "utils/log.h"

#define IMPL_TEST_INIT_LOGS_1 \
    spdlog::drop_all(); \
    auto test_logger = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>(); \
    velia::utils::initLogs(test_logger);

#define IMPL_TEST_INIT_LOGS_2 \
    spdlog::set_pattern("%S.%e [%t %n %L] %v"); \
    spdlog::set_level(spdlog::level::trace); \
    spdlog::get("sysrepo")->set_level(spdlog::level::info); \
    trompeloeil::stream_tracer tracer{std::cout};

#define TEST_INIT_LOGS \
    IMPL_TEST_INIT_LOGS_1 \
    IMPL_TEST_INIT_LOGS_2

#define TEST_SYSREPO_INIT_LOGS \
    IMPL_TEST_INIT_LOGS_1 \
    velia::utils::initLogsSysrepo(); \
    IMPL_TEST_INIT_LOGS_2
