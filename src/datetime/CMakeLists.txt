option(SLG_DATETIME_BUILD_STATIC "Build the static variant of slg_datetime" ON)
option(SLG_DATETIME_BUILD_SHARED "Build the shared variant of slg_datetime" ON)

if(NOT SLG_DATETIME_BUILD_STATIC AND NOT SLG_DATETIME_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_DATETIME_BUILD_STATIC or SLG_DATETIME_BUILD_SHARED")
endif()

set(SLG_DATETIME_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/datetime.cpp
    ${PROJECT_SOURCE_DIR}/third_party/date-3.0.4/src/tz.cpp
)

set(SLG_DATETIME_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/datetime/datetime_export.h
    ${PROJECT_SOURCE_DIR}/include/datetime/datetime.h
)

set(SLG_DATETIME_THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party/date-3.0.4/include)

function(slg_datetime_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_DATETIME_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_DATETIME_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${SLG_DATETIME_THIRD_PARTY_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_compile_definitions(${target_name}
        PRIVATE BUILD_TZ_LIB USE_OS_TZDB=1)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_DATETIME_BUILD_SHARED
            PUBLIC SLG_DATETIME_SHARED
        )
    endif()
endfunction()

if(SLG_DATETIME_BUILD_STATIC)
    slg_datetime_define_library(slg_datetime_static STATIC)
    set_target_properties(slg_datetime_static PROPERTIES OUTPUT_NAME slg_datetime_static)
    add_library(slg_datetime::static ALIAS slg_datetime_static)
endif()

if(SLG_DATETIME_BUILD_SHARED)
    slg_datetime_define_library(slg_datetime_shared SHARED)
    set_target_properties(slg_datetime_shared PROPERTIES OUTPUT_NAME slg_datetime)
    add_library(slg_datetime::shared ALIAS slg_datetime_shared)
endif()
