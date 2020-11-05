project(velia-systemstate LANGUAGES CXX)

find_package(Boost REQUIRED)
find_package(docopt REQUIRED)
find_package(spdlog REQUIRED)
find_package(sdbus-c++ REQUIRED)
find_package(PkgConfig)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(velia-system-state STATIC
    inputs/AbstractInput.cpp
    inputs/DbusSemaphoreInput.cpp
    inputs/DbusSystemdInput.cpp
    manager/AbstractManager.cpp
    manager/StateManager.cpp
    outputs/LedSysfsDriver.cpp
    outputs/callables.cpp
    State.cpp
    )
target_link_libraries(velia-system-state PUBLIC velia-utils Boost::boost SDBusCpp::sdbus-c++ ${STD_FILESYSTEM_LIBRARY})
target_include_directories(velia-system-state PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/velia-systemstate)

add_executable(velia-systemstated main.cpp)
target_link_libraries(velia-systemstated PUBLIC velia-utils velia-system-state docopt)
add_dependencies(velia-systemstated target-VELIA_VERSION)
target_include_directories(velia-systemstated PUBLIC ${CMAKE_BINARY_DIR})

# Testing
include(CTest)
if(BUILD_TESTING)
    function(systemstate_test name)
        add_executable(test-${name} ${CMAKE_SOURCE_DIR}/tests/${name}.cpp)
        target_link_libraries(test-${name} DoctestIntegration ${ARGN})
        target_include_directories(test-${name} PRIVATE ${CMAKE_BINARY_DIR})

        if(NOT CMAKE_CROSSCOMPILING)
            add_test(test-${name} test-${name})
        endif()
    endfunction()
    systemstate_test(systemstate_state-manager velia-system-state)
    systemstate_test(systemstate_input-semaphore velia-system-state DbusTesting)
    systemstate_test(systemstate_input-systemd velia-system-state DbusTesting)
    systemstate_test(systemstate_output-led velia-system-state)
endif()

install(TARGETS
    velia-systemstated
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
