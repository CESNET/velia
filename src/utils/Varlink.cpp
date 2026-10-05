/*
 * Copyright (C) 2026 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <memory>
#include <stdexcept>
#include <system_error>
#include <systemd/sd-json.h>
#include <systemd/sd-varlink.h>
#include "Varlink.h"

namespace velia::utils {

Varlink::Varlink(std::string address)
    : m_address(std::move(address))
{
}

/** @brief Calls a method and returns the reply parameters serialized as JSON.
 *
 * A new connection is created for every call.
 *
 * @param method Fully qualified method name, e.g., io.systemd.Network.GetLLDPNeighbors
 * @param paramsJson Method parameters as a JSON object
 * @throws std::system_error when the call fails, std::runtime_error when the method returns an error
 */
std::string Varlink::call(const std::string& method, const std::string& paramsJson) const
{
    sd_varlink* rawVarlink = nullptr;
    if (auto r = sd_varlink_connect_address(&rawVarlink, m_address.c_str()); r < 0) {
        throw std::system_error(-r, std::system_category(), "sd_varlink_connect_address");
    }

    std::unique_ptr<sd_varlink, decltype(&sd_varlink_unref)> varlink{rawVarlink, sd_varlink_unref};
    sd_json_variant* rawParams = nullptr;

    if (auto r = sd_json_parse(paramsJson.c_str(), sd_json_parse_flags_t{}, &rawParams, nullptr, nullptr); r < 0) {
        throw std::system_error(-r, std::system_category(), "sd_json_parse");
    }

    std::unique_ptr<sd_json_variant, decltype(&sd_json_variant_unref)> params{rawParams, sd_json_variant_unref};
    sd_json_variant* reply = nullptr; // owned by the connection
    const char* errorId = nullptr;

    if (auto r = sd_varlink_call(varlink.get(), method.c_str(), params.get(), &reply, &errorId); r < 0) {
        throw std::system_error(-r, std::system_category(), "sd_varlink_call");
    }

    if (errorId) {
        throw std::runtime_error("Varlink method '" + method + "' failed: " + errorId);
    }

    char* rawOut = nullptr;
    if (auto r = sd_json_variant_format(reply, sd_json_format_flags_t{}, &rawOut); r < 0) {
        throw std::system_error(-r, std::system_category(), "sd_json_variant_format");
    }

    std::unique_ptr<char, decltype(&free)> out{rawOut, free};
    return out.get();
}
}
