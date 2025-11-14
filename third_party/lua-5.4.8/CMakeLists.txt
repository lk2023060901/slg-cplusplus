cmake_minimum_required(VERSION 3.16)

project(lua-5.4.8 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(LUA_BUILD_STATIC "Build the Lua static library" ON)
option(LUA_BUILD_SHARED "Build the Lua shared library" ON)
option(LUA_BUILD_TOOLS "Build lua and luac executables" OFF)

set(LUA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(LUA_CORE_SOURCES
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lutf8lib.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)

set(LUA_PLATFORM_DEFINES)
if(WIN32)
    list(APPEND LUA_PLATFORM_DEFINES LUA_USE_WINDOWS)
else()
    if(APPLE)
        list(APPEND LUA_PLATFORM_DEFINES LUA_USE_MACOSX)
    else()
        list(APPEND LUA_PLATFORM_DEFINES LUA_USE_POSIX LUA_USE_DLOPEN)
    endif()
endif()

function(slg_configure_lua_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            ${LUA_SRC_DIR}
    )
    target_compile_definitions(${target_name}
        PRIVATE
            ${LUA_PLATFORM_DEFINES}
    )
    if(MSVC)
        target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(NOT WIN32)
        target_link_libraries(${target_name} PRIVATE m)
        if(NOT APPLE)
            target_link_libraries(${target_name} PRIVATE dl)
        endif()
    endif()
endfunction()

set(LUA_PRIMARY_TARGET "")

if(LUA_BUILD_SHARED)
    add_library(lua_shared SHARED ${LUA_CORE_SOURCES})
    slg_configure_lua_target(lua_shared)
    if(WIN32)
        target_compile_definitions(lua_shared PRIVATE LUA_BUILD_AS_DLL)
    endif()
    set_target_properties(lua_shared
        PROPERTIES
            OUTPUT_NAME lua
            C_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
    )
    if(MSVC)
        target_compile_definitions(lua_shared PUBLIC LUA_LIB)
        set_target_properties(lua_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
    set(LUA_PRIMARY_TARGET lua_shared)
endif()

if(LUA_BUILD_STATIC)
    add_library(lua_static STATIC ${LUA_CORE_SOURCES})
    slg_configure_lua_target(lua_static)
    set_target_properties(lua_static
        PROPERTIES
            OUTPUT_NAME lua
            POSITION_INDEPENDENT_CODE ON
    )
    if(NOT LUA_PRIMARY_TARGET)
        set(LUA_PRIMARY_TARGET lua_static)
    endif()
endif()

if(LUA_BUILD_TOOLS AND LUA_PRIMARY_TARGET)
    add_executable(lua ${LUA_SRC_DIR}/lua.c)
    target_link_libraries(lua PRIVATE ${LUA_PRIMARY_TARGET})
    target_include_directories(lua PRIVATE ${LUA_SRC_DIR})
    target_compile_definitions(lua PRIVATE ${LUA_PLATFORM_DEFINES})

    add_executable(luac ${LUA_SRC_DIR}/luac.c ${LUA_SRC_DIR}/lundump.c ${LUA_SRC_DIR}/lobject.c ${LUA_SRC_DIR}/lopcodes.c ${LUA_SRC_DIR}/lvm.c)
    target_link_libraries(luac PRIVATE ${LUA_PRIMARY_TARGET})
    target_include_directories(luac PRIVATE ${LUA_SRC_DIR})
    target_compile_definitions(luac PRIVATE ${LUA_PLATFORM_DEFINES})
endif()
