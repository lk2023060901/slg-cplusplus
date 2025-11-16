option(SLG_CITUS_BUILD_STATIC "Build the static variant of slg_citus" ON)
option(SLG_CITUS_BUILD_SHARED "Build the shared variant of slg_citus" ON)

if(NOT SLG_CITUS_BUILD_STATIC AND NOT SLG_CITUS_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_CITUS_BUILD_STATIC or SLG_CITUS_BUILD_SHARED")
endif()

find_package(PostgreSQL QUIET)

if(NOT PostgreSQL_FOUND)
    message(WARNING "PostgreSQL client libraries were not found. The slg_citus target will be skipped. Install libpq (PostgreSQL dev package) to enable it.")
    return()
endif()

set(SLG_CITUS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/citus_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/citus_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/citus_manager.cpp
)

set(SLG_CITUS_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/database/citus/citus_config.h
    ${PROJECT_SOURCE_DIR}/include/database/citus/citus_connection.h
    ${PROJECT_SOURCE_DIR}/include/database/citus/citus_export.h
    ${PROJECT_SOURCE_DIR}/include/database/citus/citus_manager.h
)

function(slg_citus_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_CITUS_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_CITUS_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${PostgreSQL_INCLUDE_DIRS}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_link_libraries(${target_name} PUBLIC ${PostgreSQL_LIBRARIES} slg_json_static)

    if(TARGET slg_coroutine_static)
        target_link_libraries(${target_name} PUBLIC slg_coroutine_static)
    elseif(TARGET slg_coroutine_shared)
        target_link_libraries(${target_name} PUBLIC slg_coroutine_shared)
    endif()

    if(TARGET slg_tcp_static)
        target_link_libraries(${target_name} PUBLIC slg_tcp_static)
    elseif(TARGET slg_tcp_shared)
        target_link_libraries(${target_name} PUBLIC slg_tcp_shared)
    endif()

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_CITUS_BUILD_SHARED
            PUBLIC SLG_CITUS_SHARED
        )
    endif()
endfunction()

if(SLG_CITUS_BUILD_STATIC)
    slg_citus_define_library(slg_citus_static STATIC)
    set_target_properties(slg_citus_static PROPERTIES OUTPUT_NAME slg_citus_static)
    add_library(slg_citus::static ALIAS slg_citus_static)
endif()

if(SLG_CITUS_BUILD_SHARED)
    slg_citus_define_library(slg_citus_shared SHARED)
    set_target_properties(slg_citus_shared PROPERTIES OUTPUT_NAME slg_citus)
    add_library(slg_citus::shared ALIAS slg_citus_shared)
endif()
