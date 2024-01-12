find_program(SYSREPOCTL sysrepoctl)

function(sysrepo_test)
    cmake_parse_arguments(TEST "" "NAME;FIXTURE;RESOURCE_LOCK" "COMMAND;LIBRARIES" ${ARGN})

    add_executable(test-${TEST_NAME} ${CMAKE_SOURCE_DIR}/tests/${TEST_NAME}.cpp)
    target_link_libraries(test-${TEST_NAME} ${TEST_LIBRARIES})
    target_include_directories(test-${TEST_NAME}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
            ${CMAKE_BINARY_DIR}
    )

    if(NOT CMAKE_CROSSCOMPILING)
        if(TEST_COMMAND)
            add_test(NAME test-${TEST_NAME} COMMAND ${TEST_COMMAND})
        else()
            add_test(NAME test-${TEST_NAME} COMMAND test-${TEST_NAME})
        endif()
    endif()

    if(TEST_RESOURCE_LOCK)
        set_tests_properties(test-${TEST_NAME} PROPERTIES RESOURCE_LOCK "${TEST_RESOURCE_LOCK}")
    endif()

    if(TEST_FIXTURE)
        set(test_name_preinit sysrepo:preinit:${TEST_NAME})
        set(test_name_init sysrepo:prep:${TEST_NAME})
        set(test_name_cleanup sysrepo:clean:${TEST_NAME})
        set(fixture_name sysrepo:env:${TEST_NAME})
        set(SYSREPO_REPOSITORY_PATH ${CMAKE_CURRENT_BINARY_DIR}/test_repositories/test_${TEST_NAME})
        set(SYSREPO_SHM_PREFIX ${CMAKE_PROJECT_NAME}_${TEST_NAME}_)
        set(test_cleanup_command ${CMAKE_COMMAND}
                -DTHIS_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DTEST_NAME=${TEST_NAME}
                -DSYSREPO_SHM_PREFIX=${SYSREPO_SHM_PREFIX}
                -P ${PROJECT_SOURCE_DIR}/cmake/SysrepoClean.cmake
                )

        add_test(NAME ${test_name_preinit} COMMAND ${test_cleanup_command})

        add_test(NAME ${test_name_init}
            COMMAND ${SYSREPOCTL}
            --search-dirs ${CMAKE_CURRENT_SOURCE_DIR}/yang:${CMAKE_CURRENT_SOURCE_DIR}/tests/yang
            ${${TEST_FIXTURE}})

        add_test(NAME ${test_name_cleanup} COMMAND ${test_cleanup_command})

        set_tests_properties(${test_name_preinit} PROPERTIES FIXTURES_SETUP ${fixture_name})
        set_tests_properties(${test_name_init} PROPERTIES FIXTURES_SETUP ${fixture_name} DEPENDS ${test_name_preinit})
        set_tests_properties(${test_name_cleanup} PROPERTIES FIXTURES_CLEANUP ${fixture_name})
        set_tests_properties(test-${TEST_NAME} PROPERTIES FIXTURES_REQUIRED ${fixture_name})

        set_property(TEST test-${TEST_NAME} ${test_name_init} APPEND PROPERTY ENVIRONMENT
            "SYSREPO_REPOSITORY_PATH=${SYSREPO_REPOSITORY_PATH}"
            "SYSREPO_SHM_PREFIX=${SYSREPO_SHM_PREFIX}"
        )
    endif()
endfunction()
