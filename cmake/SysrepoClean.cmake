foreach(var THIS_BINARY_DIR TEST_NAME SYSREPO_SHM_PREFIX)
    if(NOT ${var})
        message(FATAL_ERROR "${var} not specified")
    endif()
endforeach()

set(shm_files_pattern "/dev/shm/${SYSREPO_SHM_PREFIX}*")
file(GLOB shm_files ${shm_files_pattern})
set(dummy_nonexisting_file_to_silence_warnings ${CMAKE_CURRENT_BUILD_DIR}/sysrepo-dummy-non-existing-file-for-cleanup)

message(STATUS "Removing ${shm_files_pattern}")
file(REMOVE ${shm_files} ${dummy_nonexisting_file_to_silence_warnings})
message(STATUS "Removing ${THIS_BINARY_DIR}/test_repositories/test_${TEST_NAME}")
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/test_repositories/test_${TEST_NAME}")
