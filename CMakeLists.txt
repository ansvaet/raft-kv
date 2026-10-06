cmake_minimum_required(VERSION 3.15)
project(raft-kv-storage VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

# ------------------------------------------------------------------
# Зависимости: nlohmann/json и Google Test
# ------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# ------------------------------------------------------------------
# Список исходников для библиотеки (без main.cpp)
# ------------------------------------------------------------------
set(SOURCES
    src/kv/kv_store.cpp
    src/kv/state_machine.cpp
    src/raft/consensus.cpp
    src/raft/log_manager.cpp
    src/raft/node_impl.cpp
    src/raft/serialize.cpp
    src/network/virtual_transport.cpp
)

# Проверка наличия файлов (предупреждения, но не ошибки)
foreach(source ${SOURCES})
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source})
        message(WARNING "Source file not found: ${source}")
    endif()
endforeach()

# ------------------------------------------------------------------
# Статическая библиотека (используется основным приложением и тестами)
# ------------------------------------------------------------------
add_library(raft_kv_lib STATIC ${SOURCES})

target_link_libraries(raft_kv_lib PUBLIC
    nlohmann_json::nlohmann_json
    Threads::Threads
)

target_include_directories(raft_kv_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ------------------------------------------------------------------
# Основное приложение (отдельно, чтобы main.cpp не попадал в тесты)
# ------------------------------------------------------------------
add_executable(raft_kv src/main.cpp)
target_link_libraries(raft_kv PRIVATE raft_kv_lib)

# ------------------------------------------------------------------
# Настройки компилятора для основного приложения
# ------------------------------------------------------------------
if(MSVC)
    target_compile_options(raft_kv PRIVATE
        /W4
        /WX-
        /EHsc
        /wd4251
        /wd4275
    )
else()
    target_compile_options(raft_kv PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

set_target_properties(raft_kv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    DEBUG_POSTFIX "_d"
)

# ------------------------------------------------------------------
# Подключение тестов (только после определения библиотеки)
# ------------------------------------------------------------------
add_subdirectory(tests/integration)
add_subdirectory(tests/unit)
# Системные тесты
add_subdirectory(tests/systems)