/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <map>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <variant>
#include "utils/log-fwd.h"

namespace velia::system {

class RAUC {
public:
    using SlotProperties = std::map<std::string, std::variant<std::string, uint64_t, uint32_t>>;
    struct InstallNotifier {
        InstallNotifier(std::function<void(int32_t, std::string, int32_t)> progress, std::function<void(int32_t, std::string)> completed);

        std::function<void(int32_t, std::string, int32_t)> m_progressCallback;
        std::function<void(int32_t, std::string)> m_completedCallback;
    };

    explicit RAUC(sdbus::IConnection& connection);
    std::string primarySlot() const;
    std::map<std::string, SlotProperties> slotStatus() const;
    void install(const std::string& source, std::shared_ptr<InstallNotifier> cb);

private:
    std::shared_ptr<sdbus::IProxy> m_dbusObjectProxy;
    std::shared_ptr<InstallNotifier> m_installNotifier;
    velia::Log m_log;
};

}
