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

find_package(spdlog REQUIRED)
find_package(sdbus-c++ REQUIRED)
find_package(PkgConfig)

pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
if(${SYSTEMD_VERSION} VERSION_EQUAL 245)
    message(FATAL_ERROR "Systemd 245 has a bug affecting DBus method calls using sdbus-c++. Please see https://github.com/Kistler-Group/sdbus-cpp/issues/106.")
endif()

include(GNUInstallDirs)

# Targets
add_library(velia-utils STATIC
    src/utils/exceptions.cpp
    src/utils/exceptions.h
    src/utils/journal.cpp
    src/utils/journal.h
    src/utils/log.h
    src/utils/log-fwd.h
    src/utils/log-init.cpp
    src/utils/log-init.h
    )
target_link_libraries(velia-utils PUBLIC spdlog::spdlog PRIVATE PkgConfig::SYSTEMD)
target_include_directories(velia-utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/)


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
        tests/dbus-helpers/dbus_systemd_server.cpp
    )
    target_link_libraries(DbusTesting PUBLIC velia-utils SDBusCpp::sdbus-c++)
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

add_subdirectory(src/velia-systemstate)
