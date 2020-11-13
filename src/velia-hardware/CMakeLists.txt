project(velia-hardware LANGUAGES CXX)

find_package(spdlog REQUIRED)
find_package(PkgConfig)
pkg_check_modules(SYSREPO REQUIRED sysrepo-cpp>=1.4.79 IMPORTED_TARGET sysrepo)
pkg_check_modules(LIBYANG REQUIRED libyang-cpp>=1.0.190 IMPORTED_TARGET libyang)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(velia-hardware STATIC
    sysfs/Exceptions.cpp
    sysfs/Exceptions.h
    sysfs/EMMC.cpp
    sysfs/EMMC.h
    sysfs/HWMon.cpp
    sysfs/HWMon.h
    HardwareState.cpp
    HardwareState.h
    )
target_link_libraries(velia-hardware PUBLIC velia-utils ${STD_FILESYSTEM_LIBRARY})
target_include_directories(velia-hardware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/velia-hardware)

add_library(velia-sysrepo STATIC
    sysrepo/Logging.cpp
    sysrepo/Logging.h
    sysrepo/OpsCallback.cpp
    sysrepo/OpsCallback.h
)
target_link_libraries(velia-sysrepo PUBLIC velia-utils PkgConfig::SYSREPO PkgConfig::LIBYANG)

# Testing
include(CTest)
if(BUILD_TESTING)
    find_program(SYSREPOCTL sysrepoctl)
    find_program(SYSREPOCFG sysrepocfg)

    function(sysrepo_fixture_env name)
        set(test_name_init sysrepo:prep:${name})
        set(test_name_cleanup sysrepo:clean:${name})
        set(fixture_name sysrepo:env:${name})

        add_test(NAME ${test_name_init} COMMAND ${CMAKE_SOURCE_DIR}/tests/sysrepoctl-manage-module.sh ${SYSREPOCTL} ${SYSREPOCFG} prepare ${ARGN})
        set_tests_properties(${test_name_init} PROPERTIES FIXTURES_SETUP ${fixture_name} RESOURCE_LOCK sysrepo)
        add_test(NAME ${test_name_cleanup} COMMAND ${CMAKE_SOURCE_DIR}/tests/sysrepoctl-manage-module.sh ${SYSREPOCTL} ${SYSREPOCFG} uninstall ${ARGN})
        set_tests_properties(${test_name_cleanup} PROPERTIES FIXTURES_CLEANUP ${fixture_name} RESOURCE_LOCK sysrepo)

        if(sysrepo_previous_fixture_name)
            set_tests_properties(${test_name_init} PROPERTIES DEPENDS sysrepo:clean:${sysrepo_previous_fixture_name})
        endif()

        set(sysrepo_previous_fixture_name ${name} PARENT_SCOPE)
    endfunction()

    function(hardware_test name)
        add_executable(test-${name} ${CMAKE_SOURCE_DIR}/tests/${name}.cpp)
        target_link_libraries(test-${name} DoctestIntegration ${ARGN})
        target_include_directories(test-${name} PRIVATE ${CMAKE_BINARY_DIR})

        if(NOT CMAKE_CROSSCOMPILING)
            add_test(test-${name} test-${name})
        endif()
    endfunction()
    hardware_test(hardware_emmc velia-hardware)
    hardware_test(hardware_hwmon velia-hardware)

    hardware_test(hardware_hwstate velia-hardware velia-sysrepo)
    sysrepo_fixture_env(sysrepo
            YANG ${CMAKE_SOURCE_DIR}/yang/iana-hardware@2018-03-13.yang # to read identityrefs from iana-hardware we must install it in sysrepo too
            YANG ${CMAKE_SOURCE_DIR}/yang/ietf-hardware-state@2018-03-13.yang FEATURE hardware-sensor
            )
    set_tests_properties(
            test-hardware_hwstate
            PROPERTIES FIXTURES_REQUIRED sysrepo:env:sysrepo
            RESOURCE_LOCK sysrepo
    )
endif()
