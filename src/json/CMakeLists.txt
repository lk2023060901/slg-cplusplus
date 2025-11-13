option(SLG_JSON_BUILD_STATIC "Build the static variant of slg_json" ON)
option(SLG_JSON_BUILD_SHARED "Build the shared variant of slg_json" ON)

if(NOT SLG_JSON_BUILD_STATIC AND NOT SLG_JSON_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_JSON_BUILD_STATIC or SLG_JSON_BUILD_SHARED")
endif()

set(SLG_JSON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/json_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/json_value.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/json_writer.cpp
)

set(SLG_JSON_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/json/json_export.h
    ${PROJECT_SOURCE_DIR}/include/json/json_reader.h
    ${PROJECT_SOURCE_DIR}/include/json/json_value.h
    ${PROJECT_SOURCE_DIR}/include/json/json_writer.h
)

set(SLG_JSON_THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json-3.12.0/include)

function(slg_json_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_JSON_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_JSON_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${SLG_JSON_THIRD_PARTY_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_JSON_BUILD_SHARED
            PUBLIC SLG_JSON_SHARED
        )
    endif()
endfunction()

if(SLG_JSON_BUILD_STATIC)
    slg_json_define_library(slg_json_static STATIC)
    set_target_properties(slg_json_static PROPERTIES OUTPUT_NAME slg_json_static)
    add_library(slg_json::static ALIAS slg_json_static)
endif()

if(SLG_JSON_BUILD_SHARED)
    slg_json_define_library(slg_json_shared SHARED)
    set_target_properties(slg_json_shared PROPERTIES OUTPUT_NAME slg_json)
    add_library(slg_json::shared ALIAS slg_json_shared)
endif()
