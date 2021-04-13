/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <tests/configure.cmake.h>
#include "ietf-hardware/sysfs/EMMC.h"
#include "ietf-hardware/sysfs/HWMon.h"

// FIXME filename, as it is from the separate project, it can!t be merged with fake.h -> different dependencies

/** @short Intercept EMMC::attributes() access */
class FakeEMMC : public velia::ietf_hardware::sysfs::EMMC {
public:
    FakeEMMC()
        : EMMC(CMAKE_CURRENT_SOURCE_DIR "/tests/sysfs/emmc/device1") {}; // FIXME maybe an abstract ancestor?
    MAKE_CONST_MOCK0(attributes, (std::map<std::string, std::string>)(), override);
};

/** @short Intercept HWMon::attributes() access */
class FakeHWMon : public velia::ietf_hardware::sysfs::HWMon {
public:
    FakeHWMon()
        : HWMon(CMAKE_CURRENT_SOURCE_DIR "/tests/sysfs/hwmon/device1/hwmon") {}; // FIXME
    MAKE_CONST_MOCK0(attributes, (std::map<std::string, int64_t>)(), override);
    MAKE_CONST_MOCK1(attribute, int64_t(const std::string&), override);
};

#define FAKE_EMMC(DEVICE, VALUE) REQUIRE_CALL(*DEVICE, attributes()).IN_SEQUENCE(seq1).RETURN(std::map<std::string, std::string>(VALUE))
#define FAKE_HWMON(DEVICE, VALUE) REQUIRE_CALL(*DEVICE, attributes()).IN_SEQUENCE(seq1).RETURN(std::map<std::string, int64_t>(VALUE))
