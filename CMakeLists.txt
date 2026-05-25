cmake_minimum_required(VERSION 3.15)

project(anc)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(inc)

add_executable(anc
    src/main.c
)