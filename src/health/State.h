#pragma once
#include <cstdint>
#include <fmt/ostream.h>
#include <ostream>

namespace velia::health {

enum class State {
    OK,
    WARNING,
    ERROR,
    // keep sorted by severity ascending (good ---> bad)
};

std::ostream& operator<<(std::ostream& os, State state);

}
