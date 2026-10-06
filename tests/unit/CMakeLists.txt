cmake_minimum_required(VERSION 3.15)
project(raft_kv_unit_tests)

# Модульные тесты используют ту же библиотеку, что и основной проект
add_executable(unit_tests
    state_machine_test.cpp
    consensus_test.cpp
)

target_link_libraries(unit_tests PRIVATE
    raft_kv_lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

if(MSVC)
    target_compile_options(unit_tests PRIVATE /W4 /EHsc)
else()
    target_compile_options(unit_tests PRIVATE -Wall -Wextra)
endif()

set_target_properties(unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Автоматическая регистрация тестов в CTest
include(GoogleTest)
gtest_discover_tests(unit_tests)