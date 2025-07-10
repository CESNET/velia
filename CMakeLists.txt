cmake_minimum_required(VERSION 3.19)
project(velia LANGUAGES CXX)
cmake_policy(SET CMP0109 NEW)

# Build options
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Werror ${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic -Woverloaded-virtual -Wimplicit-fallthrough ${CMAKE_CXX_FLAGS}")
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "-Wsuggest-override ${CMAKE_CXX_FLAGS}")
endif()

if(CMAKE_CXX_FLAGS MATCHES "-stdlib=libc\\+\\+" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    set(STD_FILESYSTEM_LIBRARY c++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    set(STD_FILESYSTEM_LIBRARY stdc++fs)
endif()


add_custom_target(velia-version-cmake-ide
        cmake/ProjectGitVersion.cmake
        cmake/ProjectGitVersionRunner.cmake
        )
include(cmake/ProjectGitVersion.cmake)
prepare_git_version(VELIA_VERSION "2")

find_package(Doxygen)
option(WITH_DOCS "Create and install internal documentation (needs Doxygen)" ${DOXYGEN_FOUND})

find_package(date REQUIRED) # FIXME: Remove when we have STL with __cpp_lib_chrono >= 201907 (gcc 14)
find_package(Boost REQUIRED CONFIG)
find_package(spdlog REQUIRED)
find_package(sdbus-c++ "1.0.0" REQUIRED)
find_package(PkgConfig)
find_package(fmt "9.0.0" REQUIRED)
find_package(nlohmann_json REQUIRED)
pkg_check_modules(DOCOPT REQUIRED IMPORTED_TARGET docopt)

pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
if(${SYSTEMD_VERSION} VERSION_LESS 256)
    message(WARNING "LLDP neighbor listing support in systemd <= 255 is not supported and will break at runtime. Upgrade to systemd 256.")
endif()

pkg_check_modules(SYSREPO REQUIRED IMPORTED_TARGET sysrepo sysrepo-cpp>=6)
pkg_check_modules(LIBYANG REQUIRED IMPORTED_TARGET libyang libyang-cpp>=4)
pkg_check_modules(LIBNL REQUIRED IMPORTED_TARGET libnl-route-3.0)

include(GNUInstallDirs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)

set(YANG_SRCS
    yang/czechlight-system@2022-07-08.yang
    yang/iana-hardware@2018-03-13.yang
    yang/ietf-hardware@2018-03-13.yang
    yang/iana-crypt-hash@2014-08-06.yang
    yang/ietf-inet-types@2013-07-15.yang
    yang/ietf-netconf-acm@2018-02-14.yang
    yang/ietf-system@2014-08-06.yang
    yang/ietf-yang-types@2013-07-15.yang
    yang/ietf-ethertypes@2019-03-04.yang
    yang/ietf-interfaces@2018-02-20.yang
    yang/ietf-packet-fields@2019-03-04.yang
    yang/ietf-access-control-list@2019-03-04.yang
    yang/czechlight-firewall@2021-01-25.yang
    yang/czechlight-network@2025-06-06.yang
    yang/czechlight-network-sdn-roadm-line@2025-06-06.yang
    yang/czechlight-network-sdn-inline@2025-06-06.yang
    yang/czechlight-network-sdn-generic@2025-06-06.yang
    yang/iana-if-type@2017-01-19.yang
    yang/ietf-ip@2018-02-22.yang
    yang/ietf-routing@2018-03-13.yang
    yang/ietf-ipv4-unicast-routing@2018-03-13.yang
    yang/ietf-ipv6-unicast-routing@2018-03-13.yang
    yang/czechlight-lldp@2020-11-04.yang
    yang/iana-afn-safi@2013-07-04.yang
    yang/ietf-alarms@2019-09-11.yang
    yang/velia-alarms@2022-07-12.yang
    )

set(YANG_SUBMODULES
    yang/ietf-ipv6-router-advertisements@2018-03-13.yang
    )

# temporary until our CI switches to fmt>=9
add_compile_definitions(FMT_DEPRECATED_OSTREAM)

# Targets
# - utils
add_library(velia-utils STATIC
    src/utils/alarms.cpp
    src/utils/alarms.h
    src/utils/benchmark.cpp
    src/utils/benchmark.h
    src/utils/exceptions.cpp
    src/utils/exceptions.h
    src/utils/exec.cpp
    src/utils/exec.h
    src/utils/io.cpp
    src/utils/io.h
    src/utils/journal.cpp
    src/utils/journal.h
    src/utils/libyang.cpp
    src/utils/libyang.h
    src/utils/log.h
    src/utils/log.cpp
    src/utils/log-fwd.h
    src/utils/log-init.cpp
    src/utils/log-init.h
    src/utils/sysrepo.cpp
    src/utils/sysrepo.h
    src/utils/waitUntilSignalled.cpp
    src/utils/waitUntilSignalled.h
    )
target_link_libraries(velia-utils PUBLIC spdlog::spdlog PRIVATE PkgConfig::SYSTEMD PkgConfig::LIBYANG PkgConfig::SYSREPO fmt::fmt PkgConfig::DOCOPT)

# - health
add_library(velia-health STATIC
    src/health/outputs/AlarmsOutputs.cpp
    src/health/outputs/AlarmsOutputs.h
    src/health/outputs/LedSysfsDriver.cpp
    src/health/outputs/callables.cpp
    src/health/State.cpp
    src/health/SystemdUnits.cpp
    src/health/SystemdUnits.h
    )
target_link_libraries(velia-health
    PUBLIC
        velia-utils
        Boost::boost
        SDBusCpp::sdbus-c++
        ${STD_FILESYSTEM_LIBRARY}
    PRIVATE
        PkgConfig::LIBYANG
    )

# - ietf-hardware
add_library(velia-ietf-hardware STATIC
    src/ietf-hardware/sysfs/EMMC.cpp
    src/ietf-hardware/sysfs/EMMC.h
    src/ietf-hardware/sysfs/HWMon.cpp
    src/ietf-hardware/sysfs/HWMon.h
    src/ietf-hardware/sysfs/IpmiFruEEPROM.cpp
    src/ietf-hardware/sysfs/IpmiFruEEPROM.h
    src/ietf-hardware/sysfs/OnieEEPROM.cpp
    src/ietf-hardware/sysfs/OnieEEPROM.h
    src/ietf-hardware/IETFHardware.cpp
    src/ietf-hardware/IETFHardware.h
    src/ietf-hardware/FspYh.cpp
    src/ietf-hardware/FspYh.h
    src/ietf-hardware/thresholds.h
    src/ietf-hardware/thresholds.cpp
    src/ietf-hardware/thresholds_fwd.h
    )
target_link_libraries(velia-ietf-hardware
    PUBLIC
        velia-utils
        ${STD_FILESYSTEM_LIBRARY}
        Boost::boost
    PRIVATE
        PkgConfig::LIBYANG
        date::date
    )

add_library(velia-ietf-hardware-sysrepo STATIC
    src/ietf-hardware/sysrepo/Sysrepo.cpp
    src/ietf-hardware/sysrepo/Sysrepo.h
    )
target_link_libraries(velia-ietf-hardware-sysrepo
    PUBLIC
        velia-utils
        PkgConfig::SYSREPO
        PkgConfig::LIBYANG
    )

set(NOBODY_UID 65534 CACHE STRING "UID which refers to 'nobody'. Used for dropping root privileges.")
set(NOBODY_GID 65534 CACHE STRING "GID which refers to 'nobody'. Used for dropping root privileges.")

if(NOT NFT_EXECUTABLE)
    find_program(NFT_EXECUTABLE nft)
endif()
if(NOT SSH_KEYGEN_EXECUTABLE)
    find_program(SSH_KEYGEN_EXECUTABLE ssh-keygen)
endif()
if(NOT CHPASSWD_EXECUTABLE)
    find_program(CHPASSWD_EXECUTABLE chpasswd)
endif()
if(NOT SYSTEMCTL_EXECUTABLE)
    find_program(SYSTEMCTL_EXECUTABLE systemctl)
endif()
if(NOT NETWORKCTL_EXECUTABLE)
    find_program(NETWORKCTL_EXECUTABLE networkctl)
endif()
if(NOT HOSTNAMECTL_EXECUTABLE)
    find_program(HOSTNAMECTL_EXECUTABLE hostnamectl)
endif()

set(VELIA_AUTHORIZED_KEYS_FORMAT "{HOME}/.ssh/authorized_keys" CACHE STRING "pattern for determining path to users' SSH authorized_keys file. Must at least one of '{USER}' or '{HOME}' which will get replaced by the name of the user and the home directory of the user respectively.")
if(NOT VELIA_AUTHORIZED_KEYS_FORMAT)
    message(FATAL_ERROR "VELIA_AUTHORIZED_KEYS_FORMAT not set.")
endif()

if(NOT VELIA_AUTHORIZED_KEYS_FORMAT MATCHES "{USER}" AND NOT VELIA_AUTHORIZED_KEYS_FORMAT MATCHES "{HOME}")
    message(FATAL_ERROR "Wrong format string given for VELIA_AUTHORIZED_KEYS_FORMAT: at least one of {USER} or {HOME} expected.")
endif()

set(VELIA_REAL_ETC_PASSWD "/etc/passwd" CACHE STRING "Path to passwd file")
if(NOT VELIA_REAL_ETC_PASSWD)
    message(FATAL_ERROR "VELIA_REAL_ETC_PASSWD not set")
endif()

set(VELIA_REAL_ETC_SHADOW "/etc/shadow" CACHE STRING "Path to shadow file")
if(NOT VELIA_REAL_ETC_SHADOW)
    message(FATAL_ERROR "VELIA_REAL_ETC_SHADOW not set")
endif()

set(VELIA_BACKUP_ETC_SHADOW "/etc/shadow" CACHE STRING "Path to where /etc/shadow gets copied to upon each change")
if(NOT VELIA_BACKUP_ETC_SHADOW)
    message(FATAL_ERROR "VELIA_BACKUP_ETC_SHADOW not set")
endif()

set(VELIA_BACKUP_ETC_HOSTNAME "/etc/hostname" CACHE STRING "Path to hostname file")
if(NOT VELIA_BACKUP_ETC_HOSTNAME )
    message(FATAL_ERROR "VELIA_BACKUP_ETC_HOSTNAME  not set")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/system/system_vars.h.in ${CMAKE_CURRENT_BINARY_DIR}/system_vars.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# - ietf-system
add_library(velia-system STATIC
        src/system/RAUC.cpp
        src/system/RAUC.h
        src/system/Firmware.cpp
        src/system/Firmware.h
        src/system/IETFSystem.cpp
        src/system/IETFSystem.h
        src/system/Authentication.cpp
        src/system/Authentication.h
        src/system/JournalUpload.cpp
        src/system/JournalUpload.h
        src/system/LED.cpp
        src/system/LED.h
        )
target_link_libraries(velia-system
    PUBLIC
        velia-utils
        PkgConfig::SYSREPO
        PkgConfig::LIBYANG
        SDBusCpp::sdbus-c++
        nlohmann_json::nlohmann_json
    PRIVATE
        date::date-tz
    )

# - firewall
add_library(velia-firewall STATIC
    src/firewall/Firewall.cpp
    )
target_link_libraries(velia-firewall
    PUBLIC
    velia-utils
    PkgConfig::SYSREPO
    PRIVATE
    PkgConfig::LIBYANG
    )

add_library(velia-network STATIC
        src/network/Rtnetlink.cpp
        src/network/Rtnetlink.h
        src/network/IETFInterfaces.cpp
        src/network/IETFInterfaces.h
        src/network/IETFInterfacesConfig.cpp
        src/network/IETFInterfacesConfig.h
        src/network/LLDP.cpp
        src/network/LLDP.h
        src/network/LLDPSysrepo.cpp
        src/network/LLDPSysrepo.h
        src/network/NetworkctlUtils.cpp
        src/network/NetworkctlUtils.h
)
target_link_libraries(velia-network
    PUBLIC
        velia-utils
        PkgConfig::SYSREPO
        PkgConfig::LIBYANG
        PkgConfig::LIBNL
    )


# - daemons
add_executable(veliad-health
    src/main-health.cpp
    src/health/Factory.cpp
    src/health/Factory.h
    )
target_link_libraries(veliad-health
    PUBLIC
        velia-health
        PkgConfig::DOCOPT
    )
add_dependencies(veliad-health target-VELIA_VERSION)
target_include_directories(veliad-health PUBLIC ${CMAKE_BINARY_DIR})

add_executable(veliad-system
    src/main-system.cpp
    )
target_link_libraries(veliad-system
    PUBLIC
        velia-system
        PkgConfig::DOCOPT
    )
add_dependencies(veliad-system target-VELIA_VERSION)
target_include_directories(veliad-system PUBLIC ${CMAKE_BINARY_DIR})

add_executable(veliad-network
    src/main-network.cpp
    )
target_link_libraries(veliad-network
    PUBLIC
        velia-network
        PkgConfig::DOCOPT
    )
add_dependencies(veliad-network target-VELIA_VERSION)
target_include_directories(veliad-network PUBLIC ${CMAKE_BINARY_DIR})

add_executable(veliad-firewall
    src/main-firewall.cpp
    )
target_link_libraries(veliad-firewall
    PUBLIC
        velia-firewall
        velia-utils
        PkgConfig::DOCOPT
    )
add_dependencies(veliad-firewall target-VELIA_VERSION)
target_include_directories(veliad-firewall PUBLIC ${CMAKE_BINARY_DIR})

add_executable(veliad-hardware
    src/main-hardware.cpp
    src/ietf-hardware/Factory.cpp
    src/ietf-hardware/Factory.h
    )
target_link_libraries(veliad-hardware
    PUBLIC
        velia-ietf-hardware
        velia-ietf-hardware-sysrepo
        PkgConfig::DOCOPT
    )
add_dependencies(veliad-hardware target-VELIA_VERSION)
target_include_directories(veliad-hardware PUBLIC ${CMAKE_BINARY_DIR})

add_executable(velia-eeprom
    src/main-eeprom.cpp
    )
add_dependencies(velia-eeprom target-VELIA_VERSION)
target_link_libraries(velia-eeprom
    PUBLIC
        velia-ietf-hardware
        PkgConfig::DOCOPT
        fmt::fmt
    )
add_dependencies(veliad-hardware target-VELIA_VERSION)
target_include_directories(veliad-hardware PUBLIC ${CMAKE_BINARY_DIR})

# Testing
include(CTest)
if(BUILD_TESTING)
    find_package(trompeloeil 45 REQUIRED)
    find_package(doctest 2.4.11 REQUIRED)
    include(cmake/SysrepoTest.cmake)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/configure.cmake.h.in ${CMAKE_CURRENT_BINARY_DIR}/tests/configure.cmake.h)

    add_library(DoctestIntegration STATIC
        tests/doctest_integration.cpp
        tests/trompeloeil_doctest.h
        tests/wait-a-bit-longer.cpp
        )
    target_link_libraries(DoctestIntegration doctest::doctest spdlog::spdlog)
    target_include_directories(DoctestIntegration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/)
    target_compile_definitions(DoctestIntegration PUBLIC DOCTEST_CONFIG_SUPER_FAST_ASSERTS)

    add_library(SysrepoTesting STATIC
        tests/mock/ietf_hardware.h
        tests/mock/system.h
        tests/sysrepo-helpers/common.cpp
        tests/sysrepo-helpers/common.h
        tests/sysrepo-helpers/datastore.cpp
        tests/sysrepo-helpers/datastore.h
        tests/sysrepo-helpers/notifications.cpp
        tests/sysrepo-helpers/notifications.h
        tests/sysrepo-helpers/rpc.cpp
        tests/sysrepo-helpers/rpc.h
        tests/sysrepo-helpers/alarms.cpp
        tests/sysrepo-helpers/alarms.h
        )
    target_link_libraries(SysrepoTesting DoctestIntegration PkgConfig::SYSREPO)

    add_library(DbusTesting STATIC
        tests/dbus-helpers/dbus_rauc_server.cpp
        tests/dbus-helpers/dbus_rauc_server.h
        tests/dbus-helpers/dbus_resolve1_server.cpp
        tests/dbus-helpers/dbus_resolve1_server.h
        tests/dbus-helpers/dbus_systemd_server.cpp
        tests/dbus-helpers/dbus_systemd_server.h
        )
    target_link_libraries(DbusTesting
        PUBLIC
            velia-utils
            SDBusCpp::sdbus-c++
        )

    add_library(FsTestUtils STATIC
        tests/fs-helpers/FileInjector.cpp
        tests/fs-helpers/FileInjector.h
        tests/fs-helpers/utils.cpp
        tests/fs-helpers/utils.h
        )
    target_link_libraries(FsTestUtils
        PUBLIC
            ${STD_FILESYSTEM_LIBRARY}
        )

    find_program(IPROUTE2_EXECUTABLE ip REQUIRED)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vars.h.in ${CMAKE_CURRENT_BINARY_DIR}/test_vars.h @ONLY)

    function(velia_test)
        cmake_parse_arguments(TEST "" "NAME" "" ${ARGN})
        sysrepo_test(NAME ${TEST_NAME} ${TEST_UNPARSED_ARGUMENTS})
        target_link_libraries(test-${TEST_NAME} DoctestIntegration SysrepoTesting)
    endfunction()

    function(yangdata_test)
        cmake_parse_arguments(TEST "" "NAME;DATA;EXPECTED_ERRORS" "SCHEMA" ${ARGN})

        set(COMMAND_ARGS ${TEST_SCHEMA} ${TEST_DATA})
        if (TEST_EXPECTED_ERRORS)
            list(APPEND COMMAND_ARGS --expected-errors ${TEST_EXPECTED_ERRORS})
        endif()

        add_test(NAME "yang-data:${TEST_NAME}" COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/yanglint_test.sh ${COMMAND_ARGS})
    endfunction()

    function(collect_yangdata_tests)
        cmake_parse_arguments(TEST "" "DIRECTORY" "SCHEMA" ${ARGN})

        file(GLOB JSON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/yanglint/${TEST_DIRECTORY}/*.json")
        foreach(JSON_FILE ${JSON_FILES})
            string(REPLACE ".json" ".txt" EXPECTED_FILE ${JSON_FILE})
            get_filename_component(FILENAME ${JSON_FILE} NAME)
            if(EXISTS ${EXPECTED_FILE})
                yangdata_test(NAME "${TEST_DIRECTORY}/${FILENAME}" SCHEMA ${TEST_SCHEMA} DATA ${JSON_FILE} EXPECTED_ERRORS ${EXPECTED_FILE})
            else()
                yangdata_test(NAME "${TEST_DIRECTORY}/${FILENAME}" SCHEMA ${TEST_SCHEMA} DATA ${JSON_FILE})
            endif()
        endforeach()
    endfunction()

    find_program(YANGLINT_PATH yanglint)
    if (NOT YANGLINT_PATH)
        message(FATAL_ERROR "Cannot find libyang's yanglint")
    endif()
    foreach(yang_file ${YANG_SRCS})
        add_test(NAME yang-tree:${yang_file} COMMAND ${YANGLINT_PATH} -f tree ${CMAKE_CURRENT_SOURCE_DIR}/${yang_file})
    endforeach()

    velia_test(NAME system_rauc LIBRARIES velia-system DbusTesting RESOURCE_LOCK dbus-rauc)
    velia_test(NAME network_lldp LIBRARIES velia-network DbusTesting)
    velia_test(NAME network_managed-links LIBRARIES velia-network)

    velia_test(NAME hardware_thresholds LIBRARIES velia-ietf-hardware)
    velia_test(NAME hardware_eeprom LIBRARIES velia-ietf-hardware)
    velia_test(NAME hardware_emmc LIBRARIES velia-ietf-hardware FsTestUtils)
    velia_test(NAME hardware_hwmon LIBRARIES velia-ietf-hardware FsTestUtils)
    velia_test(NAME hardware_fspyh LIBRARIES velia-ietf-hardware FsTestUtils)
    velia_test(NAME hardware_ietf-hardware LIBRARIES velia-ietf-hardware)

    set(fixture_sysrepo-czechlight-lldp --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-lldp@2020-11-04.yang)
    velia_test(NAME sysrepo_network-lldp LIBRARIES velia-network DbusTesting FIXTURE fixture_sysrepo-czechlight-lldp)

    set(fixture_sysrepo-ietf-system --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-system@2014-08-06.yang)
    velia_test(NAME sysrepo_system-ietfsystem LIBRARIES velia-system DbusTesting date::date-tz FIXTURE fixture_sysrepo-ietf-system)

    set(fixture_sysrepo-czechlight-system --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-system@2022-07-08.yang --init-data ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-authentication.json)
    velia_test(NAME sysrepo_system-firmware LIBRARIES velia-system DbusTesting FIXTURE fixture_sysrepo-czechlight-system RESOURCE_LOCK dbus-rauc)
    velia_test(NAME sysrepo_authentication LIBRARIES velia-system FsTestUtils date::date-tz FIXTURE fixture_sysrepo-czechlight-system)
    velia_test(NAME sysrepo_system-leds LIBRARIES velia-system FsTestUtils FIXTURE fixture_sysrepo-czechlight-system)
    velia_test(NAME sysrepo_system-journalupload LIBRARIES velia-system FIXTURE fixture_sysrepo-czechlight-system)

    set(fixture_sysrepo-czechlight-network
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/iana-if-type@2017-01-19.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-interfaces@2018-02-20.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ip@2018-02-22.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-routing@2018-03-13.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv4-unicast-routing@2018-03-13.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv6-unicast-routing@2018-03-13.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network@2025-06-06.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-lldp@2020-11-04.yang
            --init-data ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/ietf-interfaces.json)
    velia_test(NAME sysrepo_interfaces-systemd-networkd LIBRARIES velia-network FIXTURE fixture_sysrepo-czechlight-network)

    set(COMMON_NETWORK_MODELS
        ${CMAKE_CURRENT_SOURCE_DIR}/yang/iana-if-type@2017-01-19.yang
        ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-interfaces@2018-02-20.yang
        ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ip@2018-02-22.yang
        ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network@2025-06-06.yang
        ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-routing@2018-03-13.yang
        ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv4-unicast-routing@2018-03-13.yang
        ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv6-unicast-routing@2018-03-13.yang
    )
    collect_yangdata_tests(
        DIRECTORY czechlight-network/common
        SCHEMA
          ${COMMON_NETWORK_MODELS}
    )
    collect_yangdata_tests(
        DIRECTORY czechlight-network/sdn-inline
        SCHEMA
          ${COMMON_NETWORK_MODELS}
          ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network-sdn-inline@2025-06-06.yang
    )
    collect_yangdata_tests(
        DIRECTORY czechlight-network/sdn-roadm-line
        SCHEMA
          ${COMMON_NETWORK_MODELS}
          ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network-sdn-roadm-line@2025-06-06.yang
    )
    collect_yangdata_tests(
        DIRECTORY czechlight-network/sdn-generic
        SCHEMA
          ${COMMON_NETWORK_MODELS}
          ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network-sdn-generic@2025-06-06.yang
    )

    collect_yangdata_tests(
        DIRECTORY czechlight-network/routing
        SCHEMA
          ${COMMON_NETWORK_MODELS}
    )

    yangdata_test(NAME "yang/network-data/ietf-interfaces-sdn-inline.json"
        DATA ${CMAKE_CURRENT_SOURCE_DIR}/yang/network-data/ietf-interfaces-sdn-inline.json
        SCHEMA
          ${COMMON_NETWORK_MODELS}
          ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network-sdn-inline@2025-06-06.yang
    )
    yangdata_test(NAME "yang/network-data/ietf-interfaces-sdn-roadm-line.json"
        DATA ${CMAKE_CURRENT_SOURCE_DIR}/yang/network-data/ietf-interfaces-sdn-roadm-line.json
        SCHEMA
          ${COMMON_NETWORK_MODELS}
          ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network-sdn-roadm-line@2025-06-06.yang
    )
    yangdata_test(NAME "yang/network-data/ietf-interfaces-sdn-generic"
        DATA ${CMAKE_CURRENT_SOURCE_DIR}/yang/network-data/ietf-interfaces-sdn-generic.json
        SCHEMA
          ${COMMON_NETWORK_MODELS}
          ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network-sdn-generic@2025-06-06.yang
    )

    set(fixture_sysrepo-utils
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-system@2014-08-06.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/iana-if-type@2017-01-19.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-interfaces@2018-02-20.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ip@2018-02-22.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-routing@2018-03-13.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv4-unicast-routing@2018-03-13.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv6-unicast-routing@2018-03-13.yang
            --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network@2025-06-06.yang
            --init-data ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/ietf-interfaces.json)
    velia_test(NAME utils_sysrepo LIBRARIES velia-utils SysrepoTesting FIXTURE fixture_sysrepo-utils)

    find_program(UNSHARE_EXECUTABLE unshare REQUIRED)
    find_program(MOUNT_EXECUTABLE mount REQUIRED)
    pkg_get_variable(SR_GROUP sysrepo SR_GROUP)
    if (SR_GROUP)
        set(UNSHARE_MAP_GROUP_ARG "--map-group=${SR_GROUP}")
    else()
        set(UNSHARE_MAP_GROUP_ARG "")
    endif()
    velia_test(NAME sysrepo_interfaces-libnl LIBRARIES velia-network FIXTURE fixture_sysrepo-czechlight-network
               COMMAND ${UNSHARE_EXECUTABLE} --net --mount --map-root-user ${UNSHARE_MAP_GROUP_ARG} sh -c "set -ex $<SEMICOLON> ${MOUNT_EXECUTABLE} -t sysfs none /sys $<SEMICOLON> $<TARGET_FILE:test-sysrepo_interfaces-libnl>")

    set(fixture_sysrepo-ietf-hardware
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/iana-hardware@2018-03-13.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-hardware@2018-03-13.yang
            --enable-feature hardware-sensor
            --enable-feature hardware-state
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-alarms@2019-09-11.yang
            --enable-feature alarm-history
            --enable-feature alarm-shelving
            --enable-feature alarm-summary
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/velia-alarms@2022-07-12.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/sysrepo-ietf-alarms@2022-02-17.yang)
    velia_test(NAME sysrepo_ietf-hardware LIBRARIES velia-ietf-hardware velia-ietf-hardware-sysrepo FsTestUtils FIXTURE fixture_sysrepo-ietf-hardware)

    # ctest dance for sysrepo_two-daemons: compile daemon, create a fixture for test, add the test into the fixture
    add_executable(test-sysrepo_test_merge-daemon ${CMAKE_SOURCE_DIR}/tests/sysrepo_two-daemons_daemon.cpp)
    target_link_libraries(test-sysrepo_test_merge-daemon PkgConfig::SYSREPO PkgConfig::LIBYANG)
    velia_test(NAME sysrepo_two-daemons LIBRARIES velia-ietf-hardware-sysrepo FIXTURE fixture_sysrepo-ietf-hardware)

    add_test(NAME sysrepo_test_merge-start COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_two-daemons_control.sh start)
    add_test(NAME sysrepo_test_merge-stop  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_two-daemons_control.sh stop)
    set_tests_properties(sysrepo_test_merge-start PROPERTIES FIXTURES_SETUP   sysrepo:two-daemons)
    set_tests_properties(sysrepo_test_merge-stop  PROPERTIES FIXTURES_CLEANUP sysrepo:two-daemons)
    get_property(TWO_DAEMONS_ENV TEST test-sysrepo_two-daemons PROPERTY ENVIRONMENT)
    set_property(TEST sysrepo_test_merge-start sysrepo_test_merge-stop PROPERTY ENVIRONMENT ${TWO_DAEMONS_ENV})
    set_property(TEST test-sysrepo_two-daemons APPEND PROPERTY FIXTURES_REQUIRED sysrepo:two-daemons)

    # ensure we run two-daemons merge test and ietf-hardware test sequentially and don't cleanup before everything ends
    set_tests_properties(sysrepo_test_merge-start PROPERTIES DEPENDS test-sysrepo_ietf-hardware)
    set_tests_properties(sysrepo:clean:sysrepo_two-daemons PROPERTIES DEPENDS sysrepo_test_merge-stop)

    set(fixture_sysrepo-firewall
        # Installing czechlight-firewall automatically installs ietf-access-control-list as implemented (not just
        # imported) but I still need to install it myself to enable the features.
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-access-control-list@2019-03-04.yang
            --enable-feature match-on-eth
            --enable-feature eth
            --enable-feature match-on-ipv4
            --enable-feature ipv4
            --enable-feature match-on-ipv6
            --enable-feature ipv6
            --enable-feature mixed-eth-ipv4-ipv6
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-firewall@2021-01-25.yang
    )

    velia_test(NAME sysrepo-firewall LIBRARIES velia-firewall FIXTURE fixture_sysrepo-firewall)

    set(fixture_sysrepo-alarms
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-alarms@2019-09-11.yang
            --enable-feature alarm-history
            --enable-feature alarm-shelving
            --enable-feature alarm-summary
        --install ${CMAKE_CURRENT_SOURCE_DIR}/yang/velia-alarms@2022-07-12.yang
        --install ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/sysrepo-ietf-alarms@2022-02-17.yang
    )
    velia_test(NAME health_systemd-units LIBRARIES velia-health DbusTesting FIXTURE fixture_sysrepo-alarms)
    velia_test(NAME sysrepo_health-led-alarms LIBRARIES velia-health DbusTesting FIXTURE fixture_sysrepo-alarms)

    find_program(SYSREPO_IETF_ALARMSD_EXECUTABLE sysrepo-ietf-alarmsd)
    if(SYSREPO_IETF_ALARMSD_EXECUTABLE)
        velia_test(NAME sysrepo_ietf-alarms LIBRARIES velia-health DbusTesting FIXTURE fixture_sysrepo-alarms)

        add_test(NAME sysrepo_ietf-alarms-start COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_sysrepo-ietf-alarms_control.sh start ${SYSREPO_IETF_ALARMSD_EXECUTABLE} ${CMAKE_BINARY_DIR}/sysrepo-ietf-alarmsd.log)
        add_test(NAME sysrepo_ietf-alarms-stop  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_sysrepo-ietf-alarms_control.sh stop)
        get_property(IETF_ALARMS_ENV TEST test-sysrepo_ietf-alarms PROPERTY ENVIRONMENT)
        set_property(TEST sysrepo_ietf-alarms-start sysrepo_ietf-alarms-stop PROPERTY ENVIRONMENT ${IETF_ALARMS_ENV})
        set_tests_properties(sysrepo_ietf-alarms-start PROPERTIES FIXTURES_SETUP   sysrepo-ietf-alarmsd)
        set_tests_properties(sysrepo_ietf-alarms-stop  PROPERTIES FIXTURES_CLEANUP sysrepo-ietf-alarmsd)
        set_property(TEST test-sysrepo_ietf-alarms APPEND PROPERTY FIXTURES_REQUIRED sysrepo-ietf-alarmsd)

        set_tests_properties(sysrepo_ietf-alarms-start PROPERTIES DEPENDS sysrepo:prep:sysrepo_ietf-alarms)
        set_tests_properties(sysrepo:clean:sysrepo_ietf-alarms PROPERTIES DEPENDS sysrepo_ietf-alarms-stop)
    endif()
endif()

if(WITH_DOCS)
    set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${doxyfile_in} ${doxyfile} @ONLY)
    add_custom_target(doc
            COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
            SOURCES ${doxyfile_in}
            )
endif()

set(YANG_DIR ${CMAKE_INSTALL_PREFIX}/share/yang/modules/velia)
install(FILES ${YANG_SRCS} ${YANG_SUBMODULES} DESTINATION ${YANG_DIR})
set(YANG_DATA_DIR ${CMAKE_INSTALL_PREFIX}/share/yang/static-data/velia)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-authentication.json DESTINATION ${YANG_DATA_DIR})
install(TARGETS
    veliad-health
    veliad-system
    veliad-network
    veliad-hardware
    veliad-firewall
    velia-eeprom
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})

set(JQ_SRCS)
function(add_jq_utility NAME)
    set(JQ_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cli/${NAME}.jq)
    set(JQ_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/cli/${NAME}.in)
    list(APPEND JQ_SRCS ${JQ_FILE} ${JQ_WRAPPER})
    configure_file(${JQ_WRAPPER} ${CMAKE_CURRENT_BINARY_DIR}/${NAME} @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${NAME}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
    install(FILES ${JQ_FILE} DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBEXECDIR}/velia)
endfunction()
add_custom_target(jq_utils SOURCES ${JQ_SRCS})

find_program(JQ_EXECUTABLE jq)
function(add_jq_test UTILITY_NAME TESTCASE_NAME)
    set(test_name jq-${UTILITY_NAME}-${TESTCASE_NAME})
    if(JQ_EXECUTABLE STREQUAL "JQ_EXECUTABLE-NOTFOUND")
        message(WARNING "Cannot find the `jq` program, skipping test \"${test_name}\"")
        return()
    endif()
    set(test_input_prefix ${CMAKE_CURRENT_SOURCE_DIR}/tests/jq/${UTILITY_NAME}/${TESTCASE_NAME})
    if(EXISTS ${test_input_prefix}.config.json)
        set(jq_extra_config "--argjson config \"$(< ${test_input_prefix}.config.json)\"")
    else()
        set(jq_extra_config "")
    endif()
    add_test(NAME ${test_name}
        COMMAND sh -c
        "${JQ_EXECUTABLE} -f ${CMAKE_CURRENT_SOURCE_DIR}/cli/${UTILITY_NAME}.jq ${jq_extra_config} \
                < ${test_input_prefix}.json \
                | diff -u ${test_input_prefix}.expected -")
endfunction()

function(add_velia_eeprom_json_test TESTCASE_NAME)
    set(test_name velia-eeprom-json-${TESTCASE_NAME})
    if(JQ_EXECUTABLE STREQUAL "JQ_EXECUTABLE-NOTFOUND")
        message(WARNING "Cannot find the `jq` program, skipping test \"${test_name}\"")
        return()
    endif()
    set(test_input_prefix ${CMAKE_CURRENT_SOURCE_DIR}/tests/jq/${UTILITY_NAME}/${TESTCASE_NAME})
    add_test(NAME ${test_name}
        COMMAND sh -c
        "$<TARGET_FILE:velia-eeprom> --onie --json \
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysfs/eeprom/${TESTCASE_NAME}.bin \
            | jq | diff -u ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysfs/eeprom/${TESTCASE_NAME}.json -")
endfunction()

add_jq_utility(velia-list-alarms)
add_jq_test(velia-list-alarms EDFA-LINE-SDN-ID210512)
add_jq_test(velia-list-alarms active-since)
add_jq_test(velia-list-alarms cleared-before)
add_jq_test(velia-list-alarms history)
add_jq_test(velia-list-alarms PGCL25033)
add_jq_utility(velia-list-hardware)
add_jq_test(velia-list-hardware EDFA-LINE-SDN-ID210512)
add_velia_eeprom_json_test(188_0-0052_eeprom)
add_velia_eeprom_json_test(onie-marvell)
add_velia_eeprom_json_test(PGCL250303)
