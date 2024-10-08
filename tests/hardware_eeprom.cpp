/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include "trompeloeil_doctest.h"
#include <filesystem>
#include "ietf-hardware/IETFHardware.h"
#include "pretty_printers.h"
#include "test_log_setup.h"
#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("EEPROM with UID/EID")
{
    using namespace velia::ietf_hardware;
    using namespace data_reader;
    TEST_INIT_LOGS;
    const auto sysfs = CMAKE_CURRENT_SOURCE_DIR + "/tests/sysfs/"s;

    REQUIRE(*hexEEPROM(sysfs, 1, 0x5c, 16, 0, 16) == "1E70C61C941000628C2EA000A000000C");

    auto working = EepromWithUid("x:eeprom", "x", sysfs, 0, 0x52, 256, 256 - 6, 6);
    REQUIRE(working().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/serial-num", "294100B13DA3"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "enabled"},
            });

    auto missing = EepromWithUid("x:eeprom", "x", sysfs, 0, 0x53, 256, 256 - 6, 6);
    REQUIRE(missing().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "disabled"},
            });

    auto corrupted = EepromWithUid("x:eeprom", "x", sysfs, 0, 0x53, 16, 2, 6);
    REQUIRE(corrupted().data == DataTree{
                {"/ietf-hardware:hardware/component[name='x:eeprom']/class", "iana-hardware:module"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/parent", "x"},
                {"/ietf-hardware:hardware/component[name='x:eeprom']/state/oper-state", "disabled"},
            });

    REQUIRE_THROWS_WITH(EepromWithUid("x:eeprom", "x", sysfs, 0, 0x20, 256, 256 - 6, 6),
                        "EEPROM: no I2C device defined at bus 0 address 0x20");

    REQUIRE_THROWS_WITH(hexEEPROM(sysfs, 0, 0, 10, 5, 6),
                        "EEPROM: region out of range");
}
