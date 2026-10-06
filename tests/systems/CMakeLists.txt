cmake_minimum_required(VERSION 3.15)
project(system_tests)

# Обязательно находим Threads
find_package(Threads REQUIRED)

add_executable(system_tests
    functional_test.cpp
    stress_test.cpp
    reliability_test.cpp
    config_test.cpp
    system_tests_main.cpp
)

target_include_directories(system_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../integration
)

target_link_libraries(system_tests
    raft_kv_lib
    Threads::Threads
    # Явно добавляем стандартную библиотеку C++ (для MSVC)
    ${CMAKE_CXX_STANDARD_LIBRARIES}
)

# Принудительно указываем флаги для корректной работы рантайма
if(MSVC)
    target_compile_options(system_tests PRIVATE /EHsc /MDd /Zi)
    target_link_options(system_tests PRIVATE /DEBUG)
else()
    target_compile_options(system_tests PRIVATE -g -pthread)
endif()

set_target_properties(system_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)