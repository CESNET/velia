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

find_package(spdlog REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/)

# Targets
add_library(cla-utils STATIC
    src/utils/log-init.h
    src/utils/log-init.cpp
    )
target_link_libraries(cla-utils PUBLIC spdlog::spdlog)

add_executable(cla-veliad
    src/main.cpp
    )

target_link_libraries(cla-veliad PUBLIC cla-utils)

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
endif()