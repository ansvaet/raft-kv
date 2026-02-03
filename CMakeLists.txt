cmake_minimum_required(VERSION 3.15)
project(raft_kv VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Основной исполняемый файл
add_executable(raft_kv
    src/main.cpp
    src/raft/node.cpp
)