# tests/integration/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(raft_kv_integration_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Создаем тестовый исполняемый файл
add_executable(integration_tests
    test_runner.cpp
    level1_state_machine_tests.cpp
    level2_single_node_tests.cpp
    level3_cluster_tests.cpp
)

# Добавляем include директории
target_include_directories(integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

# ЯВНО добавляем все исходные файлы проекта, КРОМЕ main.cpp
target_sources(integration_tests PRIVATE
    # ${CMAKE_CURRENT_SOURCE_DIR}/../../src/main.cpp - УДАЛИТЬ ЭТУ СТРОКУ!
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/kv/kv_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/kv/state_machine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/raft/consensus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/raft/log_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/raft/node_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/raft/serialize.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/network/virtual_transport.cpp
)

# Линкуемся с библиотеками
target_link_libraries(integration_tests
    PRIVATE
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Настройки компилятора
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