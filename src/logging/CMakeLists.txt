option(SLG_LOGGING_BUILD_STATIC "Build the static variant of slg_logging" ON)
option(SLG_LOGGING_BUILD_SHARED "Build the shared variant of slg_logging" ON)

if(NOT SLG_LOGGING_BUILD_STATIC AND NOT SLG_LOGGING_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_LOGGING_BUILD_STATIC or SLG_LOGGING_BUILD_SHARED")
endif()

set(SLG_LOGGING_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/logging_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logging_manager.cpp
)

set(SLG_LOGGING_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/logging/logging_config.h
    ${PROJECT_SOURCE_DIR}/include/logging/logging_export.h
    ${PROJECT_SOURCE_DIR}/include/logging/logging_manager.h
)

set(SLG_LOGGING_THIRD_PARTY_DIRS
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json-3.12.0/include
    ${PROJECT_SOURCE_DIR}/third_party/spdlog-1.16.0/include
)

function(slg_logging_link_singleton target_name lib_type)
    if(lib_type STREQUAL "SHARED")
        set(_singleton_candidates slg_singleton_shared slg_singleton_static)
    else()
        set(_singleton_candidates slg_singleton_static slg_singleton_shared)
    endif()

    set(_linked FALSE)
    foreach(candidate IN LISTS _singleton_candidates)
        if(TARGET ${candidate})
            target_link_libraries(${target_name} PUBLIC ${candidate})
            set(_linked TRUE)
            break()
        endif()
    endforeach()

    if(NOT _linked)
        message(FATAL_ERROR "No singleton target available for logging library")
    endif()
endfunction()

function(slg_logging_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_LOGGING_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_LOGGING_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${SLG_LOGGING_THIRD_PARTY_DIRS}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)

    slg_logging_link_singleton(${target_name} ${lib_type})

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_LOGGING_BUILD_SHARED
            PUBLIC SLG_LOGGING_SHARED
        )
    endif()
endfunction()

if(SLG_LOGGING_BUILD_STATIC)
    slg_logging_define_library(slg_logging_static STATIC)
    set_target_properties(slg_logging_static PROPERTIES OUTPUT_NAME slg_logging_static)
    add_library(slg_logging::static ALIAS slg_logging_static)
endif()

if(SLG_LOGGING_BUILD_SHARED)
    slg_logging_define_library(slg_logging_shared SHARED)
    set_target_properties(slg_logging_shared PROPERTIES OUTPUT_NAME slg_logging)
    add_library(slg_logging::shared ALIAS slg_logging_shared)
endif()
