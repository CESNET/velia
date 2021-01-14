/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "system/RAUC.h"

/** @short Intercepts callbacks to RAUC::InstallNotifier */
class FakeRAUCInstallNotifier : public velia::system::RAUC::InstallNotifier {
public:
    FakeRAUCInstallNotifier()
        : velia::system::RAUC::InstallNotifier(
            [this](int32_t perc, const std::string& msg, int32_t depth) { this->progressCallback(perc, msg, depth); },
            [this](int32_t status, const std::string& lastError) { completedCallback(status, lastError); })
    {
    }
    MAKE_CONST_MOCK3(progressCallback, void(int32_t, const std::string&, int32_t));
    MAKE_CONST_MOCK2(completedCallback, void(int32_t, const std::string&));
};

#define FAKE_RAUC_PROGRESS(DEVICE, PERCENT, MSG, DEPTH) REQUIRE_CALL(*DEVICE, progressCallback(PERCENT, MSG, DEPTH)).IN_SEQUENCE(seq1)
#define FAKE_RAUC_COMPLETED(DEVICE, RETVAL, LASTERROR) REQUIRE_CALL(*DEVICE, completedCallback(RETVAL, LASTERROR)).IN_SEQUENCE(seq1)
