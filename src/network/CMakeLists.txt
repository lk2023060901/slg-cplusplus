option(SLG_TCP_BUILD_STATIC "Build the static variant of slg_tcp" ON)
option(SLG_TCP_BUILD_SHARED "Build the shared variant of slg_tcp" ON)
option(SLG_HTTP_BUILD_STATIC "Build the static variant of slg_http" ON)
option(SLG_HTTP_BUILD_SHARED "Build the shared variant of slg_http" ON)

if(NOT SLG_TCP_BUILD_STATIC AND NOT SLG_TCP_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_TCP_BUILD_STATIC or SLG_TCP_BUILD_SHARED")
endif()

if(NOT SLG_HTTP_BUILD_STATIC AND NOT SLG_HTTP_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_HTTP_BUILD_STATIC or SLG_HTTP_BUILD_SHARED")
endif()

set(BOOST_ROOT_DIR ${PROJECT_SOURCE_DIR}/third_party/boost_1_89_0)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL is required to build HTTP components")
endif()

set(SLG_TCP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/tcp_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/tcp_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/tcp_io_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/tcp_server.cpp
)

set(SLG_TCP_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/network/tcp/tcp_client.h
    ${PROJECT_SOURCE_DIR}/include/network/tcp/tcp_connection.h
    ${PROJECT_SOURCE_DIR}/include/network/tcp/tcp_export.h
    ${PROJECT_SOURCE_DIR}/include/network/tcp/tcp_io_context.h
    ${PROJECT_SOURCE_DIR}/include/network/tcp/tcp_server.h
)

set(SLG_HTTP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/http/http_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http/http_server.cpp
)

set(SLG_HTTP_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/network/http/http_client.h
    ${PROJECT_SOURCE_DIR}/include/network/http/http_export.h
    ${PROJECT_SOURCE_DIR}/include/network/http/http_server.h
)

function(slg_tcp_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_TCP_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_TCP_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${BOOST_ROOT_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_link_libraries(${target_name} PUBLIC Threads::Threads)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_TCP_BUILD_SHARED
            PUBLIC SLG_TCP_SHARED
        )
    endif()
endfunction()

function(slg_http_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_HTTP_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_HTTP_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${BOOST_ROOT_DIR}
            ${OPENSSL_INCLUDE_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_link_libraries(${target_name} PUBLIC Threads::Threads OpenSSL::SSL OpenSSL::Crypto)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_HTTP_BUILD_SHARED
            PUBLIC SLG_HTTP_SHARED
        )
    endif()
endfunction()

if(SLG_TCP_BUILD_STATIC)
    slg_tcp_define_library(slg_tcp_static STATIC)
    set_target_properties(slg_tcp_static PROPERTIES OUTPUT_NAME slg_tcp_static)
    add_library(slg_tcp::static ALIAS slg_tcp_static)
endif()

if(SLG_TCP_BUILD_SHARED)
    slg_tcp_define_library(slg_tcp_shared SHARED)
    set_target_properties(slg_tcp_shared PROPERTIES OUTPUT_NAME slg_tcp)
    add_library(slg_tcp::shared ALIAS slg_tcp_shared)
endif()

if(SLG_HTTP_BUILD_STATIC)
    slg_http_define_library(slg_http_static STATIC)
    set_target_properties(slg_http_static PROPERTIES OUTPUT_NAME slg_http_static)
    add_library(slg_http::static ALIAS slg_http_static)
endif()

if(SLG_HTTP_BUILD_SHARED)
    slg_http_define_library(slg_http_shared SHARED)
    set_target_properties(slg_http_shared PROPERTIES OUTPUT_NAME slg_http)
    add_library(slg_http::shared ALIAS slg_http_shared)
endif()
