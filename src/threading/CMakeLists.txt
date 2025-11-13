option(SLG_THREADING_BUILD_STATIC "Build the static variant of slg_threading" ON)
option(SLG_THREADING_BUILD_SHARED "Build the shared variant of slg_threading" ON)

if(NOT SLG_THREADING_BUILD_STATIC AND NOT SLG_THREADING_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_THREADING_BUILD_STATIC or SLG_THREADING_BUILD_SHARED")
endif()

set(BOOST_ROOT_DIR ${PROJECT_SOURCE_DIR}/third_party/boost_1_89_0)

set(BOOST_THREAD_SOURCES
    ${BOOST_ROOT_DIR}/libs/thread/src/future.cpp
    ${BOOST_ROOT_DIR}/libs/thread/src/pthread/once.cpp
    ${BOOST_ROOT_DIR}/libs/thread/src/pthread/once_atomic.cpp
    ${BOOST_ROOT_DIR}/libs/thread/src/pthread/thread.cpp
    ${BOOST_ROOT_DIR}/libs/thread/src/tss_null.cpp
)

find_package(Threads REQUIRED)

add_library(slg_boost_thread STATIC ${BOOST_THREAD_SOURCES})
target_include_directories(slg_boost_thread PUBLIC ${BOOST_ROOT_DIR})
target_compile_definitions(slg_boost_thread
    PUBLIC
        BOOST_ALL_NO_LIB
        BOOST_CHRONO_HEADER_ONLY
        BOOST_ERROR_CODE_HEADER_ONLY
)
target_link_libraries(slg_boost_thread PUBLIC Threads::Threads)

set(SLG_THREADING_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/thread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool.cpp
)

set(SLG_THREADING_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threading/thread.h
    ${PROJECT_SOURCE_DIR}/include/threading/thread_pool.h
    ${PROJECT_SOURCE_DIR}/include/threading/threading_export.h
)

function(slg_threading_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_THREADING_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_THREADING_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${BOOST_ROOT_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_link_libraries(${target_name} PUBLIC slg_boost_thread)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_THREADING_BUILD_SHARED
            PUBLIC SLG_THREADING_SHARED
        )
    endif()
endfunction()

if(SLG_THREADING_BUILD_STATIC)
    slg_threading_define_library(slg_threading_static STATIC)
    set_target_properties(slg_threading_static PROPERTIES OUTPUT_NAME slg_threading_static)
    add_library(slg_threading::static ALIAS slg_threading_static)
endif()

if(SLG_THREADING_BUILD_SHARED)
    slg_threading_define_library(slg_threading_shared SHARED)
    set_target_properties(slg_threading_shared PROPERTIES OUTPUT_NAME slg_threading)
    add_library(slg_threading::shared ALIAS slg_threading_shared)
endif()
