cmake_minimum_required(VERSION 3.16)
project(slg_core VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

foreach(OUTPUTCONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# Third-party libraries
add_subdirectory(third_party/lua-5.4.8)

set(protobuf_INSTALL OFF CACHE BOOL "Disable protobuf install step in super build" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "Disable protobuf tests" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "Disable protobuf conformance tests" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "Disable protobuf examples" FORCE)
set(protobuf_BUILD_PROTOBUF_BINARIES ON CACHE BOOL "Build protobuf libraries and protoc" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "Build protoc binary" FORCE)
set(protobuf_BUILD_LIBPROTOC ON CACHE BOOL "Build libprotoc" FORCE)
set(protobuf_BUILD_LIBUPB ON CACHE BOOL "Build libupb for protoc dependency" FORCE)
add_subdirectory(third_party/protobuf-33.1)

set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "Disable Recast demo build" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "Disable Recast tests" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "Disable Recast examples" FORCE)
add_subdirectory(third_party/recastnavigation-1.6.0)

# Core modules
add_subdirectory(src/singleton)
add_subdirectory(src/threading)
add_subdirectory(src/network)
add_subdirectory(src/application)
add_subdirectory(src/database/citus)
add_subdirectory(src/database/redis)
add_subdirectory(src/json)
add_subdirectory(src/logging)
add_subdirectory(src/navigation)

# Services
add_subdirectory(login)
