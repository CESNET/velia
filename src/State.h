#pragma once
#include <cstdint>

enum class State : uint32_t {
    OK,
    WARNING,
    ERROR,
    // keep sorted by severity ascending (good ---> bad)
};
