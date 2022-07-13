project(velia LANGUAGES CXX)
cmake_minimum_required(VERSION 3.19)
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
prepare_git_version(VELIA_VERSION "0.0")

find_package(Doxygen)
option(WITH_DOCS "Create and install internal documentation (needs Doxygen)" ${DOXYGEN_FOUND})

find_package(Boost REQUIRED)
find_package(docopt REQUIRED)
find_package(spdlog REQUIRED)
find_package(sdbus-c++ "1.0.0" REQUIRED)
find_package(PkgConfig)
find_package(fmt)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
if(${SYSTEMD_VERSION} VERSION_EQUAL 245)
    message(FATAL_ERROR "Systemd 245 has a bug affecting DBus method calls using sdbus-c++. Please see https://github.com/Kistler-Group/sdbus-cpp/issues/106.")
endif()
#TODO: Check patch 20333 active

pkg_check_modules(SYSREPO REQUIRED sysrepo-cpp>=1.1.0 IMPORTED_TARGET sysrepo)
pkg_check_modules(LIBYANG REQUIRED libyang-cpp>=1.1.0 IMPORTED_TARGET libyang)
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
    yang/czechlight-network@2021-02-22.yang
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

# temporary until our CI switches to fmt>=9
add_compile_definitions(FMT_DEPRECATED_OSTREAM)

# Targets
# - utils
add_library(velia-utils STATIC
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
    src/utils/time.cpp
    src/utils/time.h
    src/utils/waitUntilSignalled.cpp
    src/utils/waitUntilSignalled.h
    )
target_link_libraries(velia-utils PUBLIC spdlog::spdlog PRIVATE PkgConfig::SYSTEMD PkgConfig::SYSREPO fmt::fmt docopt)

# - health
add_library(velia-health STATIC
    src/health/alarms/alarms.cpp
    src/health/alarms/alarms.h
    src/health/alarms/SystemdUnits.cpp
    src/health/alarms/SystemdUnits.h
    src/health/outputs/AlarmsOutputs.cpp
    src/health/outputs/AlarmsOutputs.h
    src/health/outputs/LedSysfsDriver.cpp
    src/health/outputs/callables.cpp
    src/health/State.cpp
    )
target_link_libraries(velia-health
    PUBLIC
        velia-utils
        Boost::boost
        SDBusCpp::sdbus-c++
        ${STD_FILESYSTEM_LIBRARY}
        PkgConfig::LIBYANG
    )

# - ietf-hardware
add_library(velia-ietf-hardware STATIC
    src/ietf-hardware/sysfs/EMMC.cpp
    src/ietf-hardware/sysfs/EMMC.h
    src/ietf-hardware/sysfs/HWMon.cpp
    src/ietf-hardware/sysfs/HWMon.h
    src/ietf-hardware/IETFHardware.cpp
    src/ietf-hardware/IETFHardware.h
    src/ietf-hardware/FspYhPsu.cpp
    src/ietf-hardware/FspYhPsu.h
    )
target_link_libraries(velia-ietf-hardware
    PUBLIC
        velia-utils
        ${STD_FILESYSTEM_LIBRARY}
        Boost::boost
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
        src/system/Rtnetlink.cpp
        src/system/Rtnetlink.h
        src/system/IETFInterfaces.cpp
        src/system/IETFInterfaces.h
        src/system/IETFInterfacesConfig.cpp
        src/system/IETFInterfacesConfig.h
        src/system/LED.cpp
        src/system/LED.h
        src/system/LLDP.cpp
        src/system/LLDP.h
        src/system/LLDPCallback.cpp
        src/system/LLDPCallback.h
        )
target_link_libraries(velia-system
    PUBLIC
        velia-utils
        PkgConfig::SYSREPO
        PkgConfig::LIBYANG
        PkgConfig::LIBNL
        SDBusCpp::sdbus-c++
        nlohmann_json::nlohmann_json
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


# - daemons
add_executable(veliad-health
    src/main-health.cpp
    src/health/Factory.cpp
    src/health/Factory.h
    )
target_link_libraries(veliad-health
    PUBLIC
        velia-health
        docopt
    )
add_dependencies(veliad-health target-VELIA_VERSION)
target_include_directories(veliad-health PUBLIC ${CMAKE_BINARY_DIR})

add_executable(veliad-system
    src/main-system.cpp
    )
target_link_libraries(veliad-system
    PUBLIC
        velia-system
        docopt
    )
add_dependencies(veliad-system target-VELIA_VERSION)
target_include_directories(veliad-system PUBLIC ${CMAKE_BINARY_DIR})

add_executable(veliad-firewall
    src/main-firewall.cpp
    )
target_link_libraries(veliad-firewall
    PUBLIC
        velia-firewall
        velia-utils
        docopt
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
        docopt
    )
add_dependencies(veliad-hardware target-VELIA_VERSION)
target_include_directories(veliad-hardware PUBLIC ${CMAKE_BINARY_DIR})

# Testing
include(CTest)
if(BUILD_TESTING)
    find_package(trompeloeil 33 REQUIRED)
    find_package(doctest 2.3.1 REQUIRED)

    option(TEST_NETWORK_WITH_SUDO "Include ietf-routing and ietf-interfaces tests that require root privileges" "OFF")

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
        tests/mock/sysrepo/events.cpp
        tests/mock/sysrepo/events.h
        )
    target_link_libraries(SysrepoTesting DoctestIntegration PkgConfig::SYSREPO)

    add_library(DbusTesting STATIC
        tests/dbus-helpers/dbus_rauc_server.cpp
        tests/dbus-helpers/dbus_rauc_server.h
        tests/dbus-helpers/dbus_resolve1_server.cpp
        tests/dbus-helpers/dbus_resolve1_server.h
        tests/dbus-helpers/dbus_semaphore_server.cpp
        tests/dbus-helpers/dbus_semaphore_server.h
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

    find_program(SYSREPOCTL sysrepoctl)
    find_program(SYSREPOCFG sysrepocfg)

    function(velia_test name)
        add_executable(test-${name} ${CMAKE_SOURCE_DIR}/tests/${name}.cpp)
        target_link_libraries(test-${name} DoctestIntegration)
        target_include_directories(test-${name}
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}
            PRIVATE
                ${CMAKE_BINARY_DIR}
        )

        if(NOT CMAKE_CROSSCOMPILING)
            add_test(test-${name} test-${name})
        endif()

        set(test_name_init sysrepo:prep:${name})
        set(test_name_cleanup sysrepo:clean:${name})
        set(fixture_name sysrepo:env:${name})

        # How to do this:
        # Check ARGN for name of fixture
        # then take the args for manage module from variable "fixture{fixture_name}"
        # and then add set env vars for sysrepo
        # set them ALSO for the init/cleanup tests
        if(ARGC GREATER 1)
            set(wanted_fixture ${ARGV1})
            add_test(NAME ${test_name_init} COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepoctl-manage-module.sh ${SYSREPOCTL} ${SYSREPOCFG} prepare ${${wanted_fixture}})
            set_tests_properties(${test_name_init} PROPERTIES FIXTURES_SETUP ${fixture_name})
            add_test(NAME ${test_name_cleanup} COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepoctl-manage-module.sh ${SYSREPOCTL} ${SYSREPOCFG} uninstall ${${wanted_fixture}})
            set_tests_properties(${test_name_cleanup} PROPERTIES FIXTURES_CLEANUP ${fixture_name})

            set_tests_properties(test-${name} PROPERTIES FIXTURES_REQUIRED ${fixture_name})
            set_property(TEST test-${name} ${test_name_init} ${test_name_cleanup} APPEND PROPERTY ENVIRONMENT
            "SYSREPO_REPOSITORY_PATH=${CMAKE_CURRENT_BINARY_DIR}/test_repositories/test_${name}"
            "SYSREPO_SHM_PREFIX=velia_test_${name}"
            )

        endif()

    endfunction()

    velia_test(health_output-led)
    target_link_libraries(test-health_output-led velia-health)

    set(fixture_sysrepo-alarms
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-alarms@2019-09-11.yang
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/velia-alarms@2022-07-12.yang
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/tests/yang/sysrepo-ietf-alarms@2022-02-17.yang
    )
    velia_test(health_systemd-units fixture_sysrepo-alarms)
    target_link_libraries(test-health_systemd-units velia-health DbusTesting)

    velia_test(sysrepo_health-led-alarms fixture_sysrepo-alarms)
    target_link_libraries(test-sysrepo_health-led-alarms velia-health DbusTesting)

    velia_test(system_rauc)
    target_link_libraries(test-system_rauc velia-system DbusTesting)
    set_tests_properties(
            test-system_rauc
            PROPERTIES RESOURCE_LOCK dbus-rauc
    )

    velia_test(system_lldp)
    target_link_libraries(test-system_lldp velia-system DbusTesting)

    set(fixture_sysrepo-czechlight-lldp YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-lldp@2020-11-04.yang)
    velia_test(sysrepo_system-lldp fixture_sysrepo-czechlight-lldp)
    target_link_libraries(test-sysrepo_system-lldp velia-system DbusTesting)

    velia_test(hardware_emmc)
    target_link_libraries(test-hardware_emmc velia-ietf-hardware FsTestUtils)
    velia_test(hardware_hwmon)
    target_link_libraries(test-hardware_hwmon velia-ietf-hardware FsTestUtils)

    set(fixture_sysrepo-ietf-system YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-system@2014-08-06.yang)
    velia_test(sysrepo_system-ietfsystem fixture_sysrepo-ietf-system)
    target_link_libraries(test-sysrepo_system-ietfsystem velia-system DbusTesting)

    set(fixture_sysrepo-czechlight-system YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-system@2022-07-08.yang)
    velia_test(sysrepo_system-firmware fixture_sysrepo-czechlight-system)
    target_link_libraries(test-sysrepo_system-firmware velia-system DbusTesting SysrepoTesting)
    set_tests_properties(
            test-sysrepo_system-firmware
            PROPERTIES RESOURCE_LOCK dbus-rauc
    )

    velia_test(sysrepo_authentication fixture_sysrepo-czechlight-system)
    target_link_libraries(test-sysrepo_authentication velia-system FsTestUtils)

    velia_test(sysrepo_system-leds fixture_sysrepo-czechlight-system)
    target_link_libraries(test-sysrepo_system-leds velia-system FsTestUtils)

    set(fixture_sysrepo-czechlight-network
            YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/iana-if-type@2017-01-19.yang
            YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-interfaces@2018-02-20.yang
            YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ip@2018-02-22.yang
            YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-routing@2018-03-13.yang
            YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv4-unicast-routing@2018-03-13.yang
            YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-ipv6-unicast-routing@2018-03-13.yang
            YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-network@2021-02-22.yang)

    velia_test(sysrepo_system-ietfinterfaces fixture_sysrepo-czechlight-network)
    target_link_libraries(test-sysrepo_system-ietfinterfaces velia-system)

    if(TEST_NETWORK_WITH_SUDO)
        find_program(SUDO_EXECUTABLE sudo REQUIRED)
        find_program(IPROUTE2_EXECUTABLE ip REQUIRED)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vars.h.in ${CMAKE_CURRENT_BINARY_DIR}/test_vars.h @ONLY)

        add_test(NAME network-czechlight0-prepare COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_system-ietfinterfaces-sudo.sh ${SUDO_EXECUTABLE})
        add_test(NAME network-czechlight0-cleanup COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_system-ietfinterfaces-sudo.sh ${SUDO_EXECUTABLE})
        set_tests_properties(network-czechlight0-prepare PROPERTIES FIXTURES_SETUP network-czechlight0)
        set_tests_properties(network-czechlight0-cleanup PROPERTIES FIXTURES_CLEANUP network-czechlight0)

        velia_test(sysrepo_system-ietfinterfaces-sudo fixture_sysrepo-czechlight-network)
        target_link_libraries(test-sysrepo_system-ietfinterfaces-sudo velia-system)
        set_property(TEST test-sysrepo_system-ietfinterfaces-sudo APPEND PROPERTY FIXTURES_REQUIRED network-czechlight0)
    endif()

    set(fixture_sysrepo-ietf-hardware YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/iana-hardware@2018-03-13.yang YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-hardware@2018-03-13.yang FEATURE hardware-sensor)
    velia_test(hardware_ietf-hardware fixture_sysrepo-ietf-hardware)
    target_link_libraries(test-hardware_ietf-hardware velia-ietf-hardware velia-ietf-hardware-sysrepo FsTestUtils)

    velia_test(sysrepo_two-daemons fixture_sysrepo-ietf-hardware)
    target_link_libraries(test-sysrepo_two-daemons velia-ietf-hardware-sysrepo)
    # ctest dance for sysrepo_two-daemons: compile daemon, create a fixture for test, add the test into the fixture
    add_executable(test-sysrepo_test_merge-daemon ${CMAKE_SOURCE_DIR}/tests/sysrepo_two-daemons_daemon.cpp)
    target_link_libraries(test-sysrepo_test_merge-daemon PkgConfig::SYSREPO PkgConfig::LIBYANG)

    add_test(NAME sysrepo_test_merge-start COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_two-daemons_control.sh start)
    add_test(NAME sysrepo_test_merge-stop  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/sysrepo_two-daemons_control.sh stop)
    set_tests_properties(sysrepo_test_merge-start PROPERTIES FIXTURES_SETUP   sysrepo:two-daemons)
    set_tests_properties(sysrepo_test_merge-stop  PROPERTIES FIXTURES_CLEANUP sysrepo:two-daemons)
    set_property(TEST sysrepo_test_merge-start sysrepo_test_merge-stop APPEND PROPERTY ENVIRONMENT
        "SYSREPO_REPOSITORY_PATH=${CMAKE_CURRENT_BINARY_DIR}/test_repositories/test_sysrepo_two-daemons"
        "SYSREPO_SHM_PREFIX=velia_test_sysrepo_two-daemons"
        )

    set_property(TEST test-sysrepo_two-daemons APPEND PROPERTY FIXTURES_REQUIRED sysrepo:two-daemons)

    # ensure we run two-daemons merge test and ietf-hardware test sequentially and don't cleanup before everything ends
    set_tests_properties(sysrepo_test_merge-start PROPERTIES DEPENDS test-hardware_ietf-hardware)
    set_tests_properties(sysrepo:clean:sysrepo_two-daemons PROPERTIES DEPENDS sysrepo_test_merge-stop)


    find_program(YANGLINT_PATH yanglint)
    if (NOT YANGLINT_PATH)
        message(FATAL_ERROR "Cannot find libyang's yanglint")
    endif()
    foreach(yang_file ${YANG_SRCS})
        add_test(NAME yang-tree:${yang_file} COMMAND ${YANGLINT_PATH} -f tree ${CMAKE_CURRENT_SOURCE_DIR}/${yang_file})
    endforeach()

    set(fixture_sysrepo-firewall
        # Installing czechlight-firewall automatically installs ietf-access-control-list as implemented (not just
        # imported) but I still need to install it myself to enable the features.
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/ietf-access-control-list@2019-03-04.yang
            FEATURE match-on-eth
            FEATURE eth
            FEATURE match-on-ipv4
            FEATURE ipv4
            FEATURE match-on-ipv6
            FEATURE ipv6
            FEATURE mixed-eth-ipv4-ipv6
        YANG ${CMAKE_CURRENT_SOURCE_DIR}/yang/czechlight-firewall@2021-01-25.yang
    )

    velia_test(sysrepo-firewall fixture_sysrepo-firewall)
    target_link_libraries(test-sysrepo-firewall velia-firewall)
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

set(YANG_DIR ${CMAKE_INSTALL_PREFIX}/share/velia/yang)
install(FILES ${YANG_SRCS} DESTINATION ${YANG_DIR})
install(FILES ${CMAKE_SOURCE_DIR}/yang/ietf-ipv6-router-advertisements@2018-03-13.yang DESTINATION ${YANG_DIR})
install(TARGETS veliad-health RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
install(TARGETS veliad-system RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
install(TARGETS veliad-hardware RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
install(TARGETS veliad-firewall RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
