/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include "system/Authentication.h"
#include "system/RAUC.h"

struct FakeRAUCInstallCb {
public:
    MAKE_CONST_MOCK2(completedCallback, void(int32_t, const std::string&));
    MAKE_CONST_MOCK1(operationCallback, void(const std::string&));
    MAKE_CONST_MOCK2(progressCallback, void(int32_t, const std::string&));
};

#define FAKE_RAUC_OPERATION(OP) REQUIRE_CALL(fakeRaucInstallCb, operationCallback(OP)).IN_SEQUENCE(seq1)
#define FAKE_RAUC_PROGRESS(PERCENT, MSG) REQUIRE_CALL(fakeRaucInstallCb, progressCallback(PERCENT, MSG)).IN_SEQUENCE(seq1)
#define FAKE_RAUC_COMPLETED(RETVAL, LASTERROR) REQUIRE_CALL(fakeRaucInstallCb, completedCallback(RETVAL, LASTERROR)).IN_SEQUENCE(seq1)

struct FakeAuthentication {
public:
    MAKE_CONST_MOCK3(changePassword, void(const std::string&, const std::string&, const std::string&));
};
