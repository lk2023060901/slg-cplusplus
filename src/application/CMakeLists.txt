option(SLG_APPLICATION_BUILD_STATIC "Build the static variant of slg_application" ON)
option(SLG_APPLICATION_BUILD_SHARED "Build the shared variant of slg_application" ON)

if(NOT SLG_APPLICATION_BUILD_STATIC AND NOT SLG_APPLICATION_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_APPLICATION_BUILD_STATIC or SLG_APPLICATION_BUILD_SHARED")
endif()

set(SLG_APPLICATION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/application.cpp
)

set(SLG_APPLICATION_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/application/application.h
    ${PROJECT_SOURCE_DIR}/include/application/application_export.h
    ${PROJECT_SOURCE_DIR}/include/application/dependency_container.h
)

set(CXXOPTS_DIR ${PROJECT_SOURCE_DIR}/third_party/cxxopts-3.3.1/include)
set(NLOHMANN_DIR ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json-3.12.0/include)

function(slg_application_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_APPLICATION_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_APPLICATION_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${CXXOPTS_DIR}
            ${NLOHMANN_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_link_libraries(${target_name} PUBLIC slg_tcp_static)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_APPLICATION_BUILD_SHARED
            PUBLIC SLG_APPLICATION_SHARED
        )
    endif()
endfunction()

if(SLG_APPLICATION_BUILD_STATIC)
    slg_application_define_library(slg_application_static STATIC)
    set_target_properties(slg_application_static PROPERTIES OUTPUT_NAME slg_application_static)
    add_library(slg_application::static ALIAS slg_application_static)
endif()

if(SLG_APPLICATION_BUILD_SHARED)
    slg_application_define_library(slg_application_shared SHARED)
    set_target_properties(slg_application_shared PROPERTIES OUTPUT_NAME slg_application)
    add_library(slg_application::shared ALIAS slg_application_shared)
endif()
