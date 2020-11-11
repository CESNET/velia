project(velia LANGUAGES CXX)
cmake_minimum_required(VERSION 3.15)

# Build options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Werror ${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic -Woverloaded-virtual ${CMAKE_CXX_FLAGS}")
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
find_package(sdbus-c++ REQUIRED)
find_package(PkgConfig)

pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
if(${SYSTEMD_VERSION} VERSION_EQUAL 245)
    message(FATAL_ERROR "Systemd 245 has a bug affecting DBus method calls using sdbus-c++. Please see https://github.com/Kistler-Group/sdbus-cpp/issues/106.")
endif()

include(GNUInstallDirs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)

# Targets
# - utils
add_library(velia-utils STATIC
    src/utils/exceptions.cpp
    src/utils/exceptions.h
    src/utils/io.cpp
    src/utils/io.h
    src/utils/journal.cpp
    src/utils/journal.h
    src/utils/log.h
    src/utils/log-fwd.h
    src/utils/log-init.cpp
    src/utils/log-init.h
    src/utils/string.cpp
    src/utils/string.h
    )
target_link_libraries(velia-utils PUBLIC spdlog::spdlog PRIVATE PkgConfig::SYSTEMD)

# - health
add_library(velia-health STATIC
    src/health/inputs/AbstractInput.cpp
    src/health/inputs/DbusSemaphoreInput.cpp
    src/health/inputs/DbusSystemdInput.cpp
    src/health/manager/AbstractManager.cpp
    src/health/manager/StateManager.cpp
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
    )

add_executable(veliad
    src/main.cpp
    )
target_link_libraries(veliad
    PUBLIC
        velia-health
        docopt
    )
add_dependencies(veliad target-VELIA_VERSION)
target_include_directories(veliad PUBLIC ${CMAKE_BINARY_DIR})

# - ietf-hardware
add_library(velia-ietf-hardware STATIC
    src/ietf-hardware/sysfs/EMMC.cpp
    src/ietf-hardware/sysfs/EMMC.h
    src/ietf-hardware/sysfs/HWMon.cpp
    src/ietf-hardware/sysfs/HWMon.h
    src/ietf-hardware/sysfs/SysfsAttributes.h
    )
target_link_libraries(velia-ietf-hardware
    PUBLIC
        velia-utils
        ${STD_FILESYSTEM_LIBRARY}
    )
target_include_directories(velia-ietf-hardware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/velia-hardware)


# Testing
include(CTest)
if(BUILD_TESTING)
    find_package(trompeloeil 33 REQUIRED)
    find_package(doctest 2.3.1 REQUIRED)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/configure.cmake.h.in ${CMAKE_CURRENT_BINARY_DIR}/tests/configure.cmake.h)

    add_library(DoctestIntegration STATIC
        tests/doctest_integration.cpp
        tests/trompeloeil_doctest.h
        tests/wait-a-bit-longer.cpp
        )
    target_link_libraries(DoctestIntegration doctest::doctest trompeloeil spdlog::spdlog)
    target_include_directories(DoctestIntegration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/)
    target_compile_definitions(DoctestIntegration PUBLIC DOCTEST_CONFIG_SUPER_FAST_ASSERTS)

    add_library(DbusTesting STATIC
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

    function(velia_test name)
        add_executable(test-${name} ${CMAKE_SOURCE_DIR}/tests/${name}.cpp)
        target_link_libraries(test-${name} DoctestIntegration ${ARGN})
        target_include_directories(test-${name}
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}
            PRIVATE
                ${CMAKE_BINARY_DIR}
        )

        if(NOT CMAKE_CROSSCOMPILING)
            add_test(test-${name} test-${name})
        endif()
    endfunction()

    velia_test(health_state-manager velia-health)
    velia_test(health_input-semaphore velia-health DbusTesting)
    velia_test(health_input-systemd velia-health DbusTesting)
    velia_test(health_output-led velia-health)

    velia_test(hardware_emmc velia-ietf-hardware)
    velia_test(hardware_hwmon velia-ietf-hardware)
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

install(TARGETS veliad RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
