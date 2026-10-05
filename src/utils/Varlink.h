/*
 * Copyright (C) 2026 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once

#include <string>

namespace velia::utils {

/** @brief Minimal wrapper around sd-varlink for synchronous method calls */
class Varlink {
public:
    explicit Varlink(std::string address);

    std::string call(const std::string& method, const std::string& paramsJson = "{}") const;

private:
    std::string m_address;
};
}
