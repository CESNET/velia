/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include <cstdio>
#include <cstdlib>
#include <inttypes.h>
#include <spdlog/sinks/systemd_sink.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "utils/journal.h"

namespace velia::utils {

/** @short Is stderr connected to journald? Not thread safe. */
bool isJournaldActive()
{
    const auto stream = ::getenv("JOURNAL_STREAM");
    if (!stream) {
        return false;
    }
    uintmax_t dev;
    uintmax_t inode;
    if (::sscanf(stream, "%" SCNuMAX ":%" SCNuMAX, &dev, &inode) != 2) {
        return false;
    }
    struct stat buf;
    if (fstat(STDERR_FILENO, &buf)) {
        return false;
    }
    return static_cast<uintmax_t>(buf.st_dev) == dev && static_cast<uintmax_t>(buf.st_ino) == inode;
}

namespace impl {
/** @short Provide better levels, see https://github.com/gabime/spdlog/pull/1292#discussion_r340777258 */
template <typename Mutex>
class journald_sink : public spdlog::sinks::systemd_sink<Mutex> {
public:
    journald_sink()
    {
        this->syslog_levels_ = {/* spdlog::level::trace      */ LOG_DEBUG,
                                /* spdlog::level::debug      */ LOG_INFO,
                                /* spdlog::level::info       */ LOG_NOTICE,
                                /* spdlog::level::warn       */ LOG_WARNING,
                                /* spdlog::level::err        */ LOG_ERR,
                                /* spdlog::level::critical   */ LOG_CRIT,
                                /* spdlog::level::off        */ LOG_ALERT};
    }
};
}

std::shared_ptr<spdlog::sinks::sink> create_journald_sink()
{
    return std::make_shared<impl::journald_sink<std::mutex>>();
}
}
