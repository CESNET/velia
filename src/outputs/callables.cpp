/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "callables.h"
#include "outputs/LedSysfsDriver.h"

namespace velia {

LedOutputCallback::LedOutputCallback(std::shared_ptr<LedSysfsDriver> red, std::shared_ptr<LedSysfsDriver> green, std::shared_ptr<LedSysfsDriver> blue)
    : m_redLed(std::move(red))
    , m_greenLed(std::move(green))
    , m_blueLed(std::move(blue))
{
    m_redLed->off();
    m_greenLed->off();
    m_blueLed->off();
}

void LedOutputCallback::operator()(State state)
{
    switch (state) {
    case State::ERROR:
        m_redLed->set(255);
        m_greenLed->off();
        m_blueLed->off();
        break;
    case State::WARNING:
        m_redLed->set(255);
        m_greenLed->set(160);
        m_blueLed->off();
        break;
    case State::OK:
        m_redLed->off();
        m_greenLed->set(255);
        m_blueLed->off();
        break;
    }
}
}