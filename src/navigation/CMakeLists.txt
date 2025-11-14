option(SLG_NAVIGATION_BUILD_STATIC "Build the static variant of slg_navigation" ON)
option(SLG_NAVIGATION_BUILD_SHARED "Build the shared variant of slg_navigation" ON)

if(NOT SLG_NAVIGATION_BUILD_STATIC AND NOT SLG_NAVIGATION_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_NAVIGATION_BUILD_STATIC or SLG_NAVIGATION_BUILD_SHARED")
endif()

set(FASTLZ_DIR ${PROJECT_SOURCE_DIR}/third_party/recastnavigation-1.6.0/RecastDemo/Contrib/fastlz)
set(FASTLZ_SOURCE ${FASTLZ_DIR}/fastlz.c)

set(SLG_NAVIGATION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/detour_nav_mesh_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/recast_nav_mesh_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/recast/BuildContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/recast/ChunkyTriMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/recast/MeshLoaderObj.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/recast/recastnavmesh.cpp
    ${FASTLZ_SOURCE}
)

set_source_files_properties(${FASTLZ_SOURCE} PROPERTIES LANGUAGE C)

set(SLG_NAVIGATION_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/navigation/detour_nav_mesh_runtime.h
    ${PROJECT_SOURCE_DIR}/include/navigation/navigation_export.h
)

function(slg_navigation_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_NAVIGATION_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_NAVIGATION_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${FASTLZ_DIR}
            ${PROJECT_SOURCE_DIR}/src
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)

    target_link_libraries(${target_name}
        PUBLIC
            RecastNavigation::DetourTileCache
            RecastNavigation::Recast
    )

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_NAVIGATION_BUILD_SHARED
            PUBLIC SLG_NAVIGATION_SHARED
        )
    endif()
endfunction()

if(SLG_NAVIGATION_BUILD_STATIC)
    slg_navigation_define_library(slg_navigation_static STATIC)
    set_target_properties(slg_navigation_static PROPERTIES OUTPUT_NAME slg_navigation_static)
    add_library(slg_navigation::static ALIAS slg_navigation_static)
endif()

if(SLG_NAVIGATION_BUILD_SHARED)
    slg_navigation_define_library(slg_navigation_shared SHARED)
    set_target_properties(slg_navigation_shared PROPERTIES OUTPUT_NAME slg_navigation)
    add_library(slg_navigation::shared ALIAS slg_navigation_shared)
endif()
