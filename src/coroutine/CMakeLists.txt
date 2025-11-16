option(SLG_COROUTINE_BUILD_STATIC "Build the static variant of slg_coroutine" ON)
option(SLG_COROUTINE_BUILD_SHARED "Build the shared variant of slg_coroutine" ON)

if(NOT SLG_COROUTINE_BUILD_STATIC AND NOT SLG_COROUTINE_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_COROUTINE_BUILD_STATIC or SLG_COROUTINE_BUILD_SHARED")
endif()

find_package(Threads REQUIRED)

# Build Boost.Context/Fiber from the vendored source tree if they have not been
# installed into the build output yet. This avoids an external Boost dependency
# and keeps the workflow consistent across platforms.
set(_boost_source_dir ${PROJECT_SOURCE_DIR}/third_party/boost_1_89_0)
set(_boost_install_dir ${CMAKE_BINARY_DIR}/third_party/boost-1.89.0)
set(_boost_config_dir ${_boost_install_dir}/lib/cmake/Boost-1.89.0)
set(_boost_config_file ${_boost_config_dir}/BoostConfig.cmake)
set(_boost_required_components fiber context atomic filesystem system chrono thread date_time)

set(_boost_missing_components OFF)
foreach(_boost_comp IN LISTS _boost_required_components)
    set(_boost_comp_config ${_boost_install_dir}/lib/cmake/boost_${_boost_comp}-1.89.0/boost_${_boost_comp}-config.cmake)
    if(NOT EXISTS ${_boost_comp_config})
        set(_boost_missing_components ON)
        break()
    endif()
endforeach()

if(NOT EXISTS ${_boost_config_file} OR _boost_missing_components)
    message(STATUS "Preparing Boost (context/fiber) from ${_boost_source_dir}")
    if(WIN32)
        set(_boost_bootstrap_cmd bootstrap.bat)
        set(_boost_b2_exe b2.exe)
    else()
        set(_boost_bootstrap_cmd ./bootstrap.sh)
        set(_boost_b2_exe ./b2)
    endif()

    if(NOT EXISTS ${_boost_source_dir}/${_boost_b2_exe})
        execute_process(
            COMMAND ${_boost_bootstrap_cmd}
            WORKING_DIRECTORY ${_boost_source_dir}
            RESULT_VARIABLE _boost_bootstrap_result
        )
        if(NOT _boost_bootstrap_result EQUAL 0)
            message(FATAL_ERROR "Failed to bootstrap Boost (b2) with exit code ${_boost_bootstrap_result}")
        endif()
    endif()

    execute_process(
        COMMAND ${_boost_b2_exe} headers
        WORKING_DIRECTORY ${_boost_source_dir}
        RESULT_VARIABLE _boost_headers_result
    )
    if(NOT _boost_headers_result EQUAL 0)
        message(FATAL_ERROR "Failed to prepare Boost headers (exit code ${_boost_headers_result})")
    endif()

    set(_boost_components --with-context --with-fiber --with-atomic --with-filesystem --with-system --with-chrono --with-thread --with-date_time)
    set(_boost_variants variant=debug,release)
    set(_boost_threading threading=multi)
    set(_boost_link link=static)

    execute_process(
        COMMAND ${_boost_b2_exe} ${_boost_components} ${_boost_variants} ${_boost_threading} ${_boost_link} install --prefix=${_boost_install_dir}
        WORKING_DIRECTORY ${_boost_source_dir}
        RESULT_VARIABLE _boost_build_result
    )
    if(NOT _boost_build_result EQUAL 0)
        message(FATAL_ERROR "Failed to build Boost components (exit code ${_boost_build_result})")
    endif()
endif()

list(PREPEND CMAKE_PREFIX_PATH ${_boost_install_dir})
set(Boost_DIR ${_boost_config_dir})
set(Boost_ROOT ${_boost_install_dir})

find_package(Boost 1.70 CONFIG REQUIRED COMPONENTS fiber context)

set(SLG_COROUTINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/actor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fiber_tcp_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/timer_bridge.cpp
)

set(SLG_COROUTINE_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/coroutine/actor.h
    ${PROJECT_SOURCE_DIR}/include/coroutine/actor_manager.h
    ${PROJECT_SOURCE_DIR}/include/coroutine/asio_bridge.h
    ${PROJECT_SOURCE_DIR}/include/coroutine/coroutine_export.h
    ${PROJECT_SOURCE_DIR}/include/coroutine/fiber_tcp_session.h
    ${PROJECT_SOURCE_DIR}/include/coroutine/mailbox.h
    ${PROJECT_SOURCE_DIR}/include/coroutine/scheduler.h
    ${PROJECT_SOURCE_DIR}/include/coroutine/timer_bridge.h
)

set(_boost_targets)
if(TARGET Boost::fiber)
    list(APPEND _boost_targets Boost::fiber Boost::context)
else()
    list(APPEND _boost_targets ${Boost_LIBRARIES})
endif()

set(_timer_target "")
if(TARGET slg_timer::static)
    set(_timer_target slg_timer::static)
elseif(TARGET slg_timer::shared)
    set(_timer_target slg_timer::shared)
endif()

function(slg_coroutine_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_COROUTINE_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_COROUTINE_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${Boost_INCLUDE_DIRS}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_compile_definitions(${target_name} PUBLIC BOOST_FIBERS_NO_LIB BOOST_CONTEXT_NO_LIB)
    target_link_libraries(${target_name} PUBLIC ${_boost_targets} Threads::Threads)
    if(NOT "${_timer_target}" STREQUAL "")
        target_link_libraries(${target_name} PUBLIC ${_timer_target})
    endif()

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_COROUTINE_BUILD_SHARED
            PUBLIC SLG_COROUTINE_SHARED
        )
    endif()
endfunction()

if(SLG_COROUTINE_BUILD_STATIC)
    slg_coroutine_define_library(slg_coroutine_static STATIC)
    set_target_properties(slg_coroutine_static PROPERTIES OUTPUT_NAME slg_coroutine_static)
    add_library(slg_coroutine::static ALIAS slg_coroutine_static)
endif()

if(SLG_COROUTINE_BUILD_SHARED)
    slg_coroutine_define_library(slg_coroutine_shared SHARED)
    set_target_properties(slg_coroutine_shared PROPERTIES OUTPUT_NAME slg_coroutine)
    add_library(slg_coroutine::shared ALIAS slg_coroutine_shared)
endif()
