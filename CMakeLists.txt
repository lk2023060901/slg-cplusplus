cmake_minimum_required(VERSION 3.16)
project(slg_core LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/singleton)
add_subdirectory(src/json)
add_subdirectory(src/logging)
