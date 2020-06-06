/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#pragma once

#include "State.h"

namespace trompeloeil {
template <>
void print(std::ostream& os, const State& state)
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
}
}