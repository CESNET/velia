#pragma once
#include <cstdint>

enum class State : uint32_t {
    UNINITIALIZED,
    OK,
    WARNING,
    ERROR,
};
