option(SLG_SINGLETON_BUILD_STATIC "Build the static variant of slg_singleton" ON)
option(SLG_SINGLETON_BUILD_SHARED "Build the shared variant of slg_singleton" ON)

if(NOT SLG_SINGLETON_BUILD_STATIC AND NOT SLG_SINGLETON_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_SINGLETON_BUILD_STATIC or SLG_SINGLETON_BUILD_SHARED")
endif()

set(SLG_SINGLETON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/singleton.cpp
)

set(SLG_SINGLETON_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/singleton/singleton.h
    ${PROJECT_SOURCE_DIR}/include/singleton/singleton_export.h
)

function(slg_singleton_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_SINGLETON_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_SINGLETON_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_SINGLETON_BUILD_SHARED
            PUBLIC SLG_SINGLETON_SHARED
        )
    endif()
endfunction()

if(SLG_SINGLETON_BUILD_STATIC)
    slg_singleton_define_library(slg_singleton_static STATIC)
    set_target_properties(slg_singleton_static PROPERTIES OUTPUT_NAME slg_singleton_static)
    add_library(slg_singleton::static ALIAS slg_singleton_static)
endif()

if(SLG_SINGLETON_BUILD_SHARED)
    slg_singleton_define_library(slg_singleton_shared SHARED)
    set_target_properties(slg_singleton_shared PROPERTIES OUTPUT_NAME slg_singleton)
    add_library(slg_singleton::shared ALIAS slg_singleton_shared)
endif()
