project(velia-hardware LANGUAGES CXX)

find_package(spdlog REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(velia-hardware STATIC
    sysfs/Exceptions.cpp
    sysfs/Exceptions.h
    sysfs/EMMC.cpp
    sysfs/EMMC.h
    sysfs/HWMon.cpp
    sysfs/HWMon.h
    )
target_link_libraries(velia-hardware PUBLIC velia-utils ${STD_FILESYSTEM_LIBRARY})
target_include_directories(velia-hardware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/velia-hardware)

# Testing
include(CTest)
if(BUILD_TESTING)
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
endif()

#install(TARGETS
#    veliad-hardwared
#    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/)
