/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "callables.h"
#include "health/outputs/LedSysfsDriver.h"

namespace velia::health {

LedOutputCallback::LedOutputCallback(std::shared_ptr<LedSysfsDriver> red, std::shared_ptr<LedSysfsDriver> green, std::shared_ptr<LedSysfsDriver> blue)
    : m_redLed(std::move(red))
    , m_greenLed(std::move(green))
    , m_blueLed(std::move(blue))
{
    reset();
}

LedOutputCallback::~LedOutputCallback()
{
    reset();
}

void LedOutputCallback::reset()
{
    m_redLed->set(0);
    m_greenLed->set(0);
    m_blueLed->set(0);
}

void LedOutputCallback::operator()(State state)
{
    switch (state) {
    case State::ERROR:
        m_redLed->set(256);
        m_greenLed->set(0);
        m_blueLed->set(0);
        break;
    case State::WARNING:
        m_redLed->set(256);
        m_greenLed->set(160);
        m_blueLed->set(0);
        break;
    case State::OK:
        m_redLed->set(0);
        m_greenLed->set(256);
        m_blueLed->set(0);
        break;
    }
}
}
