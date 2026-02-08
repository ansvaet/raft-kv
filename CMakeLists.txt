cmake_minimum_required(VERSION 3.15)
project(raft-kv-storage VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
)
FetchContent_MakeAvailable(nlohmann_json)


if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(SOURCES
    src/main.cpp
    
    src/kv/kv_store.cpp
    src/kv/state_machine.cpp
    
    src/raft/consensus.cpp
    src/raft/log_manager.cpp
    src/raft/node_impl.cpp
    
    src/network/virtual_transport.cpp
)

foreach(source ${SOURCES})
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source})
        message(WARNING "Source file not found: ${source}")
    else()
        message(STATUS "Found source: ${source}")
    endif()
endforeach()


add_executable(raft_kv ${SOURCES} "src/raft/serialize.cpp")

target_link_libraries(raft_kv PRIVATE nlohmann_json::nlohmann_json)

target_include_directories(raft_kv PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)


find_package(Threads REQUIRED)
target_link_libraries(raft_kv PRIVATE Threads::Threads)

# Настройки компилятора
if(MSVC)

    # Для Visual Studio
    target_compile_options(raft_kv PRIVATE
        /W4           # Уровень предупреждений 4
        /WX-          # Предупреждения не являются ошибками
        /EHsc         # Обработка исключений
    )
    
    # Отключаем специфичные предупреждения
    target_compile_options(raft_kv PRIVATE
        /wd4251       # class needs to have dll-interface
        /wd4275       # non dll-interface class used as base
    )
else()
    # Для GCC/Clang
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

