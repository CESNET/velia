#pragma once
#include <cstdint>
#include <ostream>
#include <spdlog/fmt/ostr.h> // allow spdlog to use operator<<(ostream, State) to print State

namespace velia {

enum class State {
    OK,
    WARNING,
    ERROR,
    // keep sorted by severity ascending (good ---> bad)
};

std::ostream& operator<<(std::ostream& os, State state);

}
