cmake_minimum_required(VERSION 3.15)
project(raft_kv_integration_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(integration_tests
    test_runner.cpp
    level1_state_machine_tests.cpp
    level2_single_node_tests.cpp
    level3_cluster_tests.cpp
)

target_link_libraries(integration_tests PRIVATE
    raft_kv_lib
)

target_include_directories(integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

if(MSVC)
    target_compile_options(integration_tests PRIVATE
        /W4
        /WX-
        /EHsc
        /wd4251
        /wd4275
    )
else()
    target_compile_options(integration_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

set_target_properties(integration_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_test(NAME integration_tests COMMAND integration_tests)
set_tests_properties(integration_tests PROPERTIES TIMEOUT 120)