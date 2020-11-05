/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#pragma once

#include <memory>
#include "health/State.h"

namespace velia::health {

class LedSysfsDriver;

class LedOutputCallback {
public:
    LedOutputCallback(std::shared_ptr<LedSysfsDriver> red, std::shared_ptr<LedSysfsDriver> green, std::shared_ptr<LedSysfsDriver> blue);
    ~LedOutputCallback();
    void reset();
    void operator()(State state);

private:
    std::shared_ptr<LedSysfsDriver> m_redLed, m_greenLed, m_blueLed;
};

}
