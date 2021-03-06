#include "health/State.h"

namespace velia::health {

std::ostream& operator<<(std::ostream& os, State state)
{
    os << "State::";
    switch (state) {
    case State::ERROR:
        os << "ERROR";
        break;
    case State::WARNING:
        os << "WARNING";
        break;
    case State::OK:
        os << "OK";
        break;
    }

    return os;
}

}
