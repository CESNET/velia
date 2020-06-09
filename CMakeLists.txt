project(velia LANGUAGES CXX)
cmake_minimum_required(VERSION 3.12)

# Build options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Werror ${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic -Woverloaded-virtual ${CMAKE_CXX_FLAGS}")
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "-Wsuggest-override ${CMAKE_CXX_FLAGS}")
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
find_package(spdlog REQUIRED)
find_package(sdbus-c++ REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)

# Targets
add_library(cla-utils STATIC
    src/utils/log-init.h
    src/utils/log-init.cpp
    )
target_link_libraries(cla-utils PUBLIC spdlog::spdlog)

add_library(cla-state-manager STATIC
    src/inputs/AbstractInput.cpp
    src/inputs/DbusSemaphoreInput.cpp
    src/manager/AbstractManager.cpp
    src/manager/StateManager.cpp
    )
target_link_libraries(cla-state-manager PUBLIC cla-utils Boost::boost SDBusCpp::sdbus-c++)


add_executable(cla-veliad
    src/main.cpp
    )

target_link_libraries(cla-veliad PUBLIC cla-utils)
add_dependencies(cla-veliad target-VELIA_VERSION)

# Testing
include(CTest)
if(BUILD_TESTING)
    enable_testing()

    find_package(trompeloeil 33 REQUIRED)
    find_package(doctest 2.3.1 REQUIRED)

    add_library(DoctestIntegration STATIC
            tests/doctest_integration.cpp
            tests/trompeloeil_doctest.h
            )
    target_include_directories(DoctestIntegration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/ ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    target_link_libraries(DoctestIntegration doctest::doctest trompeloeil spdlog::spdlog)
    target_compile_definitions(DoctestIntegration PUBLIC DOCTEST_CONFIG_SUPER_FAST_ASSERTS)

    add_library(DbusTesting STATIC
        tests/dbus-helpers/dbus_semaphore_server.cpp
    )

    function(velia_test name)
        add_executable(test_${name} tests/${name}.cpp)
        target_link_libraries(test_${name} DoctestIntegration ${ARGN})
        target_include_directories(test_${name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

        if(NOT CMAKE_CROSSCOMPILING)
            add_test(test_${name} test_${name})
        endif()

    endfunction()
    velia_test(state-manager cla-state-manager)
    velia_test(input-semaphore cla-state-manager DbusTesting)
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
