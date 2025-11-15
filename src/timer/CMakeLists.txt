option(SLG_TIMER_BUILD_STATIC "Build the static variant of slg_timer" ON)
option(SLG_TIMER_BUILD_SHARED "Build the shared variant of slg_timer" ON)

if(NOT SLG_TIMER_BUILD_STATIC AND NOT SLG_TIMER_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_TIMER_BUILD_STATIC or SLG_TIMER_BUILD_SHARED")
endif()

set(SLG_TIMER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/simple_timer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/time_wheel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cron_service.cpp
)

set(SLG_TIMER_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/timer/timer_export.h
    ${PROJECT_SOURCE_DIR}/include/timer/simple_timer.h
    ${PROJECT_SOURCE_DIR}/include/timer/time_wheel.h
    ${PROJECT_SOURCE_DIR}/include/timer/scheduler.h
    ${PROJECT_SOURCE_DIR}/include/timer/cron_service.h
)

set(SLG_TIMER_THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party/date-3.0.4/include)
set(SLG_TIMER_CRONCPP_DIR ${PROJECT_SOURCE_DIR}/third_party/croncpp-2023.03.30/include)

set(_datetime_target "")
if(TARGET slg_datetime::static)
    set(_datetime_target slg_datetime::static)
elseif(TARGET slg_datetime::shared)
    set(_datetime_target slg_datetime::shared)
endif()

function(slg_timer_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_TIMER_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_TIMER_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${SLG_TIMER_THIRD_PARTY_DIR}
            ${SLG_TIMER_CRONCPP_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    if(NOT _datetime_target STREQUAL "")
        target_link_libraries(${target_name} PUBLIC ${_datetime_target})
    endif()

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_TIMER_BUILD_SHARED
            PUBLIC SLG_TIMER_SHARED
        )
    endif()
endfunction()

if(SLG_TIMER_BUILD_STATIC)
    slg_timer_define_library(slg_timer_static STATIC)
    set_target_properties(slg_timer_static PROPERTIES OUTPUT_NAME slg_timer_static)
    add_library(slg_timer::static ALIAS slg_timer_static)
endif()

if(SLG_TIMER_BUILD_SHARED)
    slg_timer_define_library(slg_timer_shared SHARED)
    set_target_properties(slg_timer_shared PROPERTIES OUTPUT_NAME slg_timer)
    add_library(slg_timer::shared ALIAS slg_timer_shared)
endif()
