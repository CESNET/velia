#pragma once
#include <cstdint>

enum class State {
    OK,
    WARNING,
    ERROR,
    // keep sorted by severity ascending (good ---> bad)
};
