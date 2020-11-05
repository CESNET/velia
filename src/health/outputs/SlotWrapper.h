/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/
#pragma once

namespace velia::health {

class LedOutputCallback;

namespace boost::signals2 {

/**
 * Wraps a slot in a copyable class. Ensures that destructor of the Slot is called only once.
 */
template <typename Ret, typename... Args>
class SlotWrapper {
public:
    explicit SlotWrapper(std::shared_ptr<LedOutputCallback> callback)
        : m_callback(callback)
    {
    }

    Ret operator()(Args... args)
    {
        (*m_callback)(args...);
    }

private:
    std::shared_ptr<LedOutputCallback> m_callback;
};

}

}
